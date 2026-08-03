// Self-test for BcdMidi.dll. Never ships. Prints one line per check in the
// shape installer/verify uses, and exits non-zero if any check fails.
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>
#include "bcdmidi.h"

static int g_checks = 0, g_fails = 0, g_skips = 0;

static void check(bool ok, const char* what)
{
    ++g_checks;
    if (!ok) ++g_fails;
    printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
}

// A check that COULD NOT BE MEASURED, counted in the denominator and named
// with its reason - the shape installer/verify's two "NOT MEASURED HERE" lines
// already use.
//
// It exists because of a real result, not a hypothetical. On 2026-08-01 the
// controller ran the eight-check build against a machine whose MIDI service was
// already wedged, and the two post-close checks printed:
//
//     ...0 ms for the WinMM view to disappear (deadline 10000 ms)
//     [ ok ] after close: the port is gone from WinMM's INPUT list
//     [ ok ] ...and gone from the OUTPUT list
//
// Two greens, and neither had measured anything: the port had never been
// created, so "it is gone" was trivially true. That is the same defect those
// two checks were written to fix - a check that cannot tell "the close worked"
// from "there was nothing to close" - wearing the other hat. The other four
// reds gave the run away that day, but a check has to be worth something ON ITS
// OWN, because next time it might be the only one looking.
//
// Skipping rather than failing, and the argument for it: a failure is a claim
// that something is broken. If the port never appeared, the close is not broken
// - it is UNMEASURED, and saying "broken" would send somebody debugging a close
// path that was never reached. A named skip says exactly what happened, keeps
// the denominator at eight so a check that quietly stopped running is still
// visible, and cannot be read as a pass.
static void skip(const char* what, const char* why)
{
    ++g_checks;
    ++g_skips;
    printf("  [SKIP] %s\n", what);
    printf("         NOT MEASURED: %s\n", why);
}

// ------------------------------------------------------------- the round trip
//
// Two shared observations, both written by a thread that is not this one - the
// DLL's callback runs on a Windows MIDI Services thread, and MIM_DATA runs on a
// WinMM thread - so both go through a critical section. EnterCriticalSection is
// one of the few calls the MIDI callback documentation permits inside such a
// handler, which is why the handlers below do nothing else.
//
// FIRST MESSAGE, not last, and a count beside it. If something else on this
// machine is also sending to the port, keeping the last one would let a
// stranger's message overwrite ours and turn a real red into a mystery; the
// count says how many arrived so the log can tell those two apart.

static CRITICAL_SECTION g_rxLock;
static unsigned char g_rxBytes[8];
static unsigned int g_rxCount = 0;       // length of the FIRST message
static int g_rxMessages = 0;             // how many times the callback fired
static void* g_rxUser = 0;               // what the DLL handed back

// Registered as the callback's `user`. Not a null and not a 1: it has to be a
// value that can only have come from the create call, or "the user pointer
// round-tripped" would be true by accident for any implementation that passed
// nothing at all.
static char g_userToken[] = "selftest user pointer";

static void recvFromDll(void* user, const unsigned char* bytes, unsigned int count)
{
    EnterCriticalSection(&g_rxLock);
    if (g_rxMessages == 0) {
        g_rxUser = user;
        g_rxCount = count > sizeof(g_rxBytes) ? (unsigned int)sizeof(g_rxBytes) : count;
        for (unsigned int i = 0; i < g_rxCount; ++i)
            g_rxBytes[i] = bytes[i];
    }
    ++g_rxMessages;
    LeaveCriticalSection(&g_rxLock);
}

// The WinMM input keeps SEVERAL messages and not just the first, because the
// running-status check below is a claim about a SEQUENCE: two messages, in one
// order and not the other, the second of which was never given a status byte.
// One slot could not express that.
#define kMaxInMessages 8
static CRITICAL_SECTION g_inLock;
static DWORD g_inMsgs[kMaxInMessages];   // MIM_DATA, packed as WinMM packs it
static int g_inMessages = 0;             // counts ALL of them, including any past the array

static void CALLBACK midiInProc(HMIDIIN, UINT wMsg, DWORD_PTR, DWORD_PTR dwParam1, DWORD_PTR)
{
    if (wMsg != MIM_DATA)
        return;
    EnterCriticalSection(&g_inLock);
    if (g_inMessages < kMaxInMessages)
        g_inMsgs[g_inMessages] = (DWORD)dwParam1;
    // Counted even when it does not fit. A count that stopped at the array size
    // would turn "far more arrived than we sent" into "exactly what we sent",
    // and that is the one surprise this harness must not be able to hide.
    ++g_inMessages;
    LeaveCriticalSection(&g_inLock);
}

// Unpacks one MIM_DATA into the three bytes it stands for. WinMM packs a short
// message backwards from how it reads: status in the low byte, then the data.
static void unpackMidiIn(DWORD packed, unsigned char* out)
{
    out[0] = (unsigned char)(packed & 0xFF);
    out[1] = (unsigned char)((packed >> 8) & 0xFF);
    out[2] = (unsigned char)((packed >> 16) & 0xFF);
}

static void printBytes(const char* what, const unsigned char* b, unsigned int n)
{
    printf("  ...%s: %u byte(s)", what, n);
    for (unsigned int i = 0; i < n; ++i)
        printf(" %02X", b[i]);
    printf("\n");
}

// Waits until at least `want` messages have arrived on one side, and returns
// how long it really took. Same argument as waitForWinMmCount above: a deadline
// can only turn a false red into a true green, never the other way round. If
// they never arrive, the loop runs out and the checks below see the nothing
// that is really there.
static ULONGLONG waitForMessageCount(volatile int* counter, CRITICAL_SECTION* lock,
                                     int want, ULONGLONG deadlineMs)
{
    const ULONGLONG t0 = GetTickCount64();
    for (;;) {
        EnterCriticalSection(lock);
        const int seen = *counter;
        LeaveCriticalSection(lock);
        if (seen >= want) break;
        if (GetTickCount64() - t0 >= deadlineMs) break;
        Sleep(20);
    }
    return GetTickCount64() - t0;
}

// The WinMM device index carrying this exact name, or -1.
static int findWinMmOut(const wchar_t* name)
{
    UINT total = midiOutGetNumDevs();
    for (UINT i = 0; i < total; ++i) {
        MIDIOUTCAPSW c;
        if (midiOutGetDevCapsW(i, &c, sizeof(c)) == MMSYSERR_NOERROR &&
            wcscmp(c.szPname, name) == 0)
            return (int)i;
    }
    return -1;
}

static int findWinMmIn(const wchar_t* name)
{
    UINT total = midiInGetNumDevs();
    for (UINT i = 0; i < total; ++i) {
        MIDIINCAPSW c;
        if (midiInGetDevCapsW(i, &c, sizeof(c)) == MMSYSERR_NOERROR &&
            wcscmp(c.szPname, name) == 0)
            return (int)i;
    }
    return -1;
}

// Counts how many WinMM input ports carry this exact name. Exact, not
// substring: the whole point of the name work is that it comes out clean.
static int countWinMmIn(const wchar_t* name)
{
    int n = 0;
    UINT total = midiInGetNumDevs();
    for (UINT i = 0; i < total; ++i) {
        MIDIINCAPSW c;
        if (midiInGetDevCapsW(i, &c, sizeof(c)) == MMSYSERR_NOERROR &&
            wcscmp(c.szPname, name) == 0)
            ++n;
    }
    return n;
}

static int countWinMmOut(const wchar_t* name)
{
    int n = 0;
    UINT total = midiOutGetNumDevs();
    for (UINT i = 0; i < total; ++i) {
        MIDIOUTCAPSW c;
        if (midiOutGetDevCapsW(i, &c, sizeof(c)) == MMSYSERR_NOERROR &&
            wcscmp(c.szPname, name) == 0)
            ++n;
    }
    return n;
}

// Every WinMM port name, printed once per snapshot. The counted checks below
// answer yes/no; this is the denominator behind them, so a red line can be
// read against the whole list instead of against nothing.
static void dumpWinMm(const char* when)
{
    UINT nIn = midiInGetNumDevs();
    UINT nOut = midiOutGetNumDevs();
    printf("  --- WinMM %s: %u input port(s), %u output port(s) ---\n", when, nIn, nOut);
    for (UINT i = 0; i < nIn; ++i) {
        MIDIINCAPSW c;
        if (midiInGetDevCapsW(i, &c, sizeof(c)) == MMSYSERR_NOERROR)
            printf("      IN  [%u] \"%ws\"\n", i, c.szPname);
        else
            printf("      IN  [%u] <midiInGetDevCapsW failed>\n", i);
    }
    for (UINT i = 0; i < nOut; ++i) {
        MIDIOUTCAPSW c;
        if (midiOutGetDevCapsW(i, &c, sizeof(c)) == MMSYSERR_NOERROR)
            printf("      OUT [%u] \"%ws\"\n", i, c.szPname);
        else
            printf("      OUT [%u] <midiOutGetDevCapsW failed>\n", i);
    }
}

// Waits until WinMM's view of this name reaches `want` on BOTH sides, or the
// deadline runs out, and returns how long that really took.
//
// It POLLS rather than sleeping a flat 1500 ms, and the difference matters in
// one direction only: a deadline can turn a false red into a true green, and
// can NEVER turn a red into a green. If the count never reaches `want` the loop
// simply runs out and the counted checks that follow see whatever is really
// there. Nothing about what is asserted changes - only how long this is willing
// to wait before asserting it. A flat guess at the latency of something nobody
// had measured would make those checks flaky, and on this machine a re-run is
// not free: closing a virtual port can wedge the MIDI service until reboot.
//
// The elapsed figure comes from GetTickCount64 and not from counting sleeps.
// Adding up kPollMs would undercount, because each turn of the loop also pays
// for two full WinMM enumerations, and those go to the service.
static ULONGLONG waitForWinMmCount(const wchar_t* name, int want, ULONGLONG deadlineMs)
{
    const ULONGLONG t0 = GetTickCount64();
    for (;;) {
        if (countWinMmIn(name) == want && countWinMmOut(name) == want) break;
        if (GetTickCount64() - t0 >= deadlineMs) break;
        Sleep(250);
    }
    return GetTickCount64() - t0;
}

int main()
{
    const wchar_t* kName = L"BCD3000 SELFTEST";
    const ULONGLONG kDeadlineMs = 10000;
    // A message crossing the service has nothing to enumerate and nothing to
    // publish, so it is orders of magnitude quicker than the port appearing.
    // Three seconds is still far beyond anything plausible; it is a deadline,
    // not an expectation, and the elapsed figure is printed either way.
    const ULONGLONG kRoundTripMs = 3000;

    // The exact three bytes of each direction, written once and compared
    // against later. Two DIFFERENT messages on purpose: if the port ever looped
    // back on itself, one shared vector would let the echo of the first
    // direction pass as the arrival of the second.
    const unsigned char kNoteOn[3] = { 0x90, 0x3C, 0x64 };   // WinMM -> DLL
    const unsigned char kCc[3]     = { 0xB0, 0x07, 0x40 };   // DLL -> WinMM

    InitializeCriticalSection(&g_rxLock);
    InitializeCriticalSection(&g_inLock);

    dumpWinMm("before");
    check(countWinMmIn(kName)  == 0, "before: no input port carries the test name");
    check(countWinMmOut(kName) == 0, "before: no output port carries the test name");

    // Both slots are seeded with poison, so "the DLL wrote zero" and "the DLL
    // never touched it" are different observations rather than the same one.
    unsigned int err = 0xDEADBEEF;
    long hr = 0x0BADF00D;
    void* port = BcdMidiCreatePort(kName, recvFromDll, g_userToken, &err, &hr);
    printf("  ...create returned port=%p err=0x%08X hr=0x%08lX\n",
           port, err, (unsigned long)hr);
    check(port != 0, "the port was created");
    check(err == 0 && hr == 0, "and BOTH result slots were set to zero on success, not left stale");
    if (port == 0)
        printf("  ...creation reported: %s\n", BcdMidiErrorText(err));

    const ULONGLONG appearedMs = waitForWinMmCount(kName, 1, kDeadlineMs);
    printf("  ...%llu ms for the WinMM view to appear (deadline %llu ms)\n",
           appearedMs, kDeadlineMs);

    dumpWinMm("after create");

    // Read the two counts ONCE and reuse them, so the visibility checks below
    // and the precondition of the post-close checks cannot disagree about what
    // WinMM said.
    const int inSeen = countWinMmIn(kName);
    const int outSeen = countWinMmOut(kName);
    check(inSeen  == 1, "the port is visible to a legacy WinMM app, as INPUT, exactly once");
    check(outSeen == 1, "...and as OUTPUT, exactly once");

    // ---- the port CARRIES BYTES, both ways ---------------------------------
    //
    // Visibility is not usefulness. Everything above this line would still pass
    // for a port that appears in every DJ application, accepts a connection,
    // and moves nothing: the whole product is the two directions below.
    //
    // WHY THE BYTES ARE COMPARED AND NOT JUST COUNTED. "A message arrived" and
    // "the right message arrived" are different claims, and only the second one
    // is worth anything to somebody whose crossfader moves the wrong control.
    // The injection this task is proved with - dropping the third byte of the
    // outgoing message - leaves the arrival TRUE and the equality FALSE, which
    // is precisely the pair of results a count alone could not tell apart.
    //
    // EVERY SKIP BELOW IS PRECEDED BY A RED. The condition for skipping the
    // round trip is that the port could not be opened, and that is itself a
    // check; the condition for skipping the open is that the port was never
    // visible, and that is the check above. So "SELFTEST_OK with skips" stays
    // unreachable, the same way it was with eight checks.
    HMIDIOUT hOut = 0;
    HMIDIIN hIn = 0;
    MMRESULT outOpen = MMSYSERR_ERROR;
    MMRESULT inReady = MMSYSERR_ERROR;

    if (outSeen >= 1) {
        const int outIdx = findWinMmOut(kName);
        if (outIdx >= 0)
            outOpen = midiOutOpen(&hOut, (UINT)outIdx, 0, 0, CALLBACK_NULL);
        printf("  ...midiOutOpen on OUT index %d returned %u\n", outIdx, (unsigned int)outOpen);
        check(outIdx >= 0 && outOpen == MMSYSERR_NOERROR,
              "a legacy WinMM app can OPEN the port's output side");
    } else {
        skip("a legacy WinMM app can OPEN the port's output side",
             "the port never appeared in WinMM's OUTPUT list, so there was nothing to open");
    }

    if (inSeen >= 1) {
        const int inIdx = findWinMmIn(kName);
        MMRESULT inOpen = MMSYSERR_ERROR, started = MMSYSERR_ERROR;
        if (inIdx >= 0)
            inOpen = midiInOpen(&hIn, (UINT)inIdx, (DWORD_PTR)midiInProc, 0, CALLBACK_FUNCTION);
        if (inOpen == MMSYSERR_NOERROR)
            started = midiInStart(hIn);
        printf("  ...midiInOpen on IN index %d returned %u, midiInStart returned %u\n",
               inIdx, (unsigned int)inOpen, (unsigned int)started);
        // STARTED, not merely opened. An input that is open but not started
        // delivers nothing, so treating "open" as enough would make the two
        // checks below unmeasurable while looking measured.
        if (inOpen == MMSYSERR_NOERROR && started == MMSYSERR_NOERROR)
            inReady = MMSYSERR_NOERROR;
        check(inReady == MMSYSERR_NOERROR,
              "a legacy WinMM app can OPEN and START the port's input side");
    } else {
        skip("a legacy WinMM app can OPEN and START the port's input side",
             "the port never appeared in WinMM's INPUT list, so there was nothing to open");
    }

    // ---- direction one: a WinMM app sends, the DLL's callback receives ------
    if (outOpen == MMSYSERR_NOERROR) {
        // WinMM packs a short message as status in the low byte, then the two
        // data bytes. 0x90 0x3C 0x64 - Note On, middle C, velocity 100.
        const DWORD packed = (DWORD)kNoteOn[0]
                           | ((DWORD)kNoteOn[1] << 8)
                           | ((DWORD)kNoteOn[2] << 16);
        const MMRESULT sent = midiOutShortMsg(hOut, packed);
        printf("  ...midiOutShortMsg(0x%06lX) returned %u\n",
               (unsigned long)packed, (unsigned int)sent);

        const ULONGLONG rxMs = waitForMessageCount(&g_rxMessages, &g_rxLock, 1, kRoundTripMs);

        EnterCriticalSection(&g_rxLock);
        const int rxMessages = g_rxMessages;
        const unsigned int rxCount = g_rxCount;
        unsigned char rxBytes[8];
        memcpy(rxBytes, g_rxBytes, sizeof(rxBytes));
        void* rxUser = g_rxUser;
        LeaveCriticalSection(&g_rxLock);

        printf("  ...%llu ms for the DLL's callback; it fired %d time(s), user=%p (registered %p)\n",
               rxMs, rxMessages, rxUser, (void*)g_userToken);
        printBytes("the DLL's callback received", rxBytes, rxCount);

        check(rxMessages >= 1 && rxUser == (void*)g_userToken,
              "WinMM -> DLL: the receive callback fired, with the user pointer it was registered with");
        check(rxCount == 3 &&
              rxBytes[0] == kNoteOn[0] && rxBytes[1] == kNoteOn[1] && rxBytes[2] == kNoteOn[2],
              "WinMM -> DLL: ...and it carried exactly 90 3C 64, three bytes, in that order");
    } else {
        skip("WinMM -> DLL: the receive callback fired, with the user pointer it was registered with",
             "the port's output side could not be opened, so nothing was ever sent into it");
        skip("WinMM -> DLL: ...and it carried exactly 90 3C 64, three bytes, in that order",
             "the port's output side could not be opened, so nothing was ever sent into it");
    }

    // ---- direction two: the DLL sends, a WinMM app receives -----------------
    if (inReady == MMSYSERR_NOERROR) {
        const int sendOk = BcdMidiSend(port, kCc, 3);
        printf("  ...BcdMidiSend(B0 07 40) returned %d\n", sendOk);
        // A separate check from the arrival, and worth its own line: it is the
        // difference between a send that admitted it failed and a send that
        // claimed success while nothing left the process. bcdmidi.h promises
        // the second can never happen; this is where that promise is measured.
        check(sendOk != 0, "DLL -> WinMM: BcdMidiSend reported that the bytes left");

        const ULONGLONG inMs = waitForMessageCount(&g_inMessages, &g_inLock, 1, kRoundTripMs);

        EnterCriticalSection(&g_inLock);
        const int inMessages = g_inMessages;
        const DWORD inFirst = g_inMsgs[0];
        LeaveCriticalSection(&g_inLock);

        unsigned char got[3];
        unpackMidiIn(inFirst, got);
        printf("  ...%llu ms for the WinMM input; it fired %d time(s), first dwParam1=0x%08lX\n",
               inMs, inMessages, (unsigned long)inFirst);
        printBytes("the WinMM input received", got, inMessages >= 1 ? 3u : 0u);

        check(inMessages >= 1, "DLL -> WinMM: a message reached the legacy WinMM input");
        check(inMessages >= 1 &&
              got[0] == kCc[0] && got[1] == kCc[1] && got[2] == kCc[2],
              "DLL -> WinMM: ...and it carried exactly B0 07 40, in that order");

        // ---- RUNNING STATUS, which is the one rule this DLL owns alone -------
        //
        // Everything above sends one whole message per call, and a translator
        // that had never heard of running status would pass all of it. This is
        // the check that would not.
        //
        // WHY IT IS HERE AT ALL. The bridge's midi_to_usb is handed one
        // already-parsed message at a time; the parsing was the third-party
        // library's job and is now feedMidi1Byte's. Carrying the status byte
        // ACROSS BcdMidiSend CALLS is therefore the only piece of this task's
        // translation with no counterpart anywhere else in the product, and it
        // was shipping with nothing exercising it.
        //
        // WHAT MAKES IT A PROOF AND NOT A COINCIDENCE. The second call contains
        // two bytes, 0x0B and 0x22, and NEITHER OF THEM IS 0xB1. If a message
        // whose status byte is B1 comes out of the port after that call, the
        // only place that B1 can have come from is state the DLL kept from the
        // call before it. Nothing else in this file can produce it.
        //
        // A THIRD DISTINCT PAIR OF MESSAGES, on purpose. Direction one uses
        // 90 3C 64 and the first half of direction two uses B0 07 40; these use
        // status B1, which appears nowhere else in this harness. If the port
        // ever looped back on itself, no echo of either earlier message could
        // be mistaken for either of these.
        const unsigned char kRunSeed[3] = { 0xB1, 0x0A, 0x11 };   // B1 0A 11
        const unsigned char kRunTail[2] = { 0x0B, 0x22 };         // -> B1 0B 22
        const unsigned char kRunWant2[3] = { 0xB1, 0x0B, 0x22 };

        const int seedOk = BcdMidiSend(port, kRunSeed, 3);
        printf("  ...BcdMidiSend(B1 0A 11) returned %d\n", seedOk);
        check(seedOk != 0,
              "DLL -> WinMM: the running-status seed B1 0A 11 reported that the bytes left");

        if (seedOk != 0) {
            const int tailOk = BcdMidiSend(port, kRunTail, 2);
            printf("  ...BcdMidiSend(0B 22 - TWO DATA BYTES, NO STATUS) returned %d\n", tailOk);
            // This return value alone is already a measurement of running
            // status: with no status byte carried over, feedMidi1Byte would
            // have refused both bytes, nothing would have been completed, and
            // BcdMidiSend would have answered 0 - the same answer midi_to_usb
            // gives to b"\x40\x40\x40".
            check(tailOk != 0,
                  "DLL -> WinMM: ...and a following send of TWO DATA BYTES WITH NO STATUS also reported that the bytes left");

            const ULONGLONG runMs = waitForMessageCount(&g_inMessages, &g_inLock, 3, kRoundTripMs);

            EnterCriticalSection(&g_inLock);
            const int runMessages = g_inMessages;
            const DWORD runFirst = g_inMsgs[1];
            const DWORD runSecond = g_inMsgs[2];
            LeaveCriticalSection(&g_inLock);

            unsigned char got2[3], got3[3];
            unpackMidiIn(runFirst, got2);
            unpackMidiIn(runSecond, got3);
            printf("  ...%llu ms; the WinMM input has now fired %d time(s) in total\n",
                   runMs, runMessages);
            printBytes("the WinMM input received, message 2", got2, runMessages >= 2 ? 3u : 0u);
            printBytes("the WinMM input received, message 3", got3, runMessages >= 3 ? 3u : 0u);

            check(runMessages >= 3,
                  "DLL -> WinMM: both running-status messages reached the legacy WinMM input");
            check(runMessages >= 3 &&
                  got2[0] == kRunSeed[0]  && got2[1] == kRunSeed[1]  && got2[2] == kRunSeed[2] &&
                  got3[0] == kRunWant2[0] && got3[1] == kRunWant2[1] && got3[2] == kRunWant2[2],
                  "DLL -> WinMM: ...and they were exactly B1 0A 11 then B1 0B 22, in that order");
        } else {
            skip("DLL -> WinMM: ...and a following send of TWO DATA BYTES WITH NO STATUS also reported that the bytes left",
                 "the seeding send never left, so no status byte was ever established for a following one to reuse");
            skip("DLL -> WinMM: both running-status messages reached the legacy WinMM input",
                 "the seeding send never left, so the first of the two messages could not arrive either");
            skip("DLL -> WinMM: ...and they were exactly B1 0A 11 then B1 0B 22, in that order",
                 "the seeding send never left, so the first of the two messages could not arrive either");
        }
    } else {
        skip("DLL -> WinMM: BcdMidiSend reported that the bytes left",
             "the port's input side could not be opened and started, so a send could not be observed");
        skip("DLL -> WinMM: a message reached the legacy WinMM input",
             "the port's input side could not be opened and started, so nothing could arrive");
        skip("DLL -> WinMM: ...and it carried exactly B0 07 40, in that order",
             "the port's input side could not be opened and started, so nothing could arrive");
        skip("DLL -> WinMM: the running-status seed B1 0A 11 reported that the bytes left",
             "the port's input side could not be opened and started, so a send could not be observed");
        skip("DLL -> WinMM: ...and a following send of TWO DATA BYTES WITH NO STATUS also reported that the bytes left",
             "the port's input side could not be opened and started, so a send could not be observed");
        skip("DLL -> WinMM: both running-status messages reached the legacy WinMM input",
             "the port's input side could not be opened and started, so nothing could arrive");
        skip("DLL -> WinMM: ...and they were exactly B1 0A 11 then B1 0B 22, in that order",
             "the port's input side could not be opened and started, so nothing could arrive");
    }

    // The WinMM handles go FIRST, before the port they point at is destroyed.
    if (hIn) { midiInStop(hIn); midiInReset(hIn); midiInClose(hIn); }
    if (hOut) { midiOutReset(hOut); midiOutClose(hOut); }

    // ---- and now the other half of this task's title: the port CLOSES -------
    //
    // Without the two checks below, nothing here measured the close at all.
    // Every check above resolves before this line runs, so g_fails would be
    // fixed by the time BcdMidiClosePort is even called: an implementation that
    // deleted the SetEvent, or a `void BcdMidiClosePort(void*) {}`, would still
    // print zero failures. The task is "creates the port AND closes it" and
    // half of it had no measurement.
    //
    // They cost no extra create/close cycle - the close already happens - and
    // they cannot become casualties of the known service defect: WinMM
    // ENUMERATION keeps working after that wedge (measured 2026-08-01; the
    // reference probe's --list answered normally afterwards, and so did both
    // wedged runs). It is creating new virtual devices that stops, and nothing
    // is created here.
    //
    // THE PRECONDITION IS APPEARANCE, NOT CREATION, AND PER DIRECTION.
    // "The port is gone" only means something if the port was THERE, in that
    // direction, a moment ago. Two ways it can be absent while the close is
    // never exercised, and both are real runs that happened:
    //   * creation failed outright - the wedged-service case that produced two
    //     hollow greens on 2026-08-01;
    //   * creation SUCCEEDED but the port was never published to WinMM - which
    //     is exactly what CreateOnlyUmpEndpoints(true) does, and one of the
    //     injections waiting for a boot.
    // Testing `port != 0` would catch the first and sail straight past the
    // second. `inSeen`/`outSeen` catch both, and per direction, because the
    // groups injection leaves INPUT visible and takes only OUTPUT away - so on
    // that run one of these two is genuinely measurable and the other is not.
    BcdMidiClosePort(port);

    if (inSeen >= 1 || outSeen >= 1) {
        const ULONGLONG goneMs = waitForWinMmCount(kName, 0, kDeadlineMs);
        printf("  ...%llu ms for the WinMM view to disappear (deadline %llu ms)\n",
               goneMs, kDeadlineMs);
        dumpWinMm("after close");
    } else {
        printf("  ...not waiting for the port to disappear: it never appeared\n");
    }

    if (inSeen >= 1)
        check(countWinMmIn(kName) == 0, "after close: the port is gone from WinMM's INPUT list");
    else
        skip("after close: the port is gone from WinMM's INPUT list",
             "the port never appeared in WinMM's INPUT list, so its absence proves nothing about the close");

    if (outSeen >= 1)
        check(countWinMmOut(kName) == 0, "...and gone from the OUTPUT list");
    else
        skip("...and gone from the OUTPUT list",
             "the port never appeared in WinMM's OUTPUT list, so its absence proves nothing about the close");

    DeleteCriticalSection(&g_rxLock);
    DeleteCriticalSection(&g_inLock);

    // The denominator is always 19, whatever happened. A skip is inside it, so
    // a check that quietly stopped running still changes the number.
    printf("\n%d checks, %d failures, %d skipped\n", g_checks, g_fails, g_skips);

    // A skip on its own cannot hide behind a green, and that is structural
    // rather than lucky. Every skip in this file is guarded by a condition that
    // has ALREADY turned some other check red:
    //   * the two post-close skips need inSeen < 1 or outSeen < 1, and either
    //     of those has already failed the matching visibility check;
    //   * the two round-trip open skips need the same thing;
    //   * the nine round-trip byte and send skips need an open that failed, and
    //     the open is itself a check one line above them;
    //   * the three running-status skips need the seeding send to have failed,
    //     and that send is itself a check one line above them.
    // So "SELFTEST_OK with skips" is unreachable. The verdict line says so
    // anyway, because a reader should not have to reconstruct that argument.
    if (g_fails)
        printf("SELFTEST_FAIL\n");
    else if (g_skips)
        printf("SELFTEST_INCOMPLETE\n");   // unreachable by the argument above
    else
        printf("SELFTEST_OK\n");

    return g_fails ? 1 : (g_skips ? 2 : 0);
}
