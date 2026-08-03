// Shared layer for the BCD3000 installer, uninstaller and checker.
//
// Everything in this folder is written in English on purpose: the project is
// going to a public international repository, and the installer is the first
// thing a stranger runs.
//
// The three executables built from this folder are:
//   BCD3000Setup.exe      installs the driver and the control service
//   BCD3000Uninstall.exe   undoes exactly what the setup did, and nothing else
//   BCD3000Check.exe       read-only report of the machine state; changes nothing
//
// All the detection in this file is READ ONLY by design. BCD3000Check.exe exists
// so that the detection can be exercised without any risk of touching a working
// installation, and so that a support request can start with a machine report
// instead of a guess.

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// ---------------------------------------------------------------------------
// Resource identifiers of the payloads embedded in BCD3000Setup.exe.
// ---------------------------------------------------------------------------
#define IDR_PAYLOAD_ASIO_DLL    101
#define IDR_PAYLOAD_BRIDGE_EXE  102
#define IDR_PAYLOAD_UNINSTALLER 103

// The two DLLs the control service needs BESIDE IT, and they are a pair rather
// than two independent payloads: ours calls Windows MIDI Services through
// C++/WinRT, and that activation is registration-free only while Microsoft's
// runtime sits in the same directory as the process. Ship one without the other
// and the control service starts, loads, and fails at the first MIDI call.
//
// 105 IS THE ONE THING IN THIS PRODUCT THAT IS SOMEBODY ELSE'S BINARY. It is
// Copyright (c) Microsoft Corporation, from github.com/microsoft/MIDI, under the
// MIT licence - which requires its copyright notice and permission notice to be
// included with every copy. They are in the repository's LICENSE file AND in
// this program's own output, through printThirdPartyNotice() in setup.cpp, so
// that a BCD3000Setup.exe handed to somebody on its own still carries them.
#define IDR_PAYLOAD_MIDI_DLL    104
#define IDR_PAYLOAD_WINMIDI_DLL 105

// The application icon, in all three executables. Id 1 on purpose: the shell
// shows the icon with the lowest id as the file's icon.
#define IDI_APPICON             1

// The photograph of the device on the first page of the window.
//
// ONE FILE, ONE ID, ONE REFERENCE. It is embedded as the bytes of
// docs\BCD3000.PNG and decoded at run time, so replacing the picture is
// replacing that one file and rebuilding - there is no generated intermediate to
// keep in step and nothing else in the program mentions it.
//
// It is a manufacturer product photograph and is NOT covered by this project's
// licence. See installer\README.md.
#define IDR_DEVICE_PHOTO_PNG    110

// The screenshot of Zadig's window, shown on page 2 beside the walkthrough.
//
// SAME DISCIPLINE AS THE PHOTOGRAPH ABOVE, AND FOR THE SAME REASONS: one file, one
// id, one reference. The bytes of docs\Zadig.png go in unchanged and gui.cpp decodes
// them with the same WIC path, so replacing the picture is replacing that one file.
//
// A DIFFERENT LICENCE SITUATION FROM THE PHOTOGRAPH, said here so the two are not
// filed together: this one is a screenshot of the owner's own machine, taken by the
// owner, showing the owner's own device. Zadig is GPLv3 and that licence covers the
// program, not a picture of its window.
//
// *** IT IS A PICTURE OF A MACHINE THAT IS ALREADY BOUND, AND THE CAPTION HAS TO SAY
// SO. *** Two fields differ from what a user with an unbound mixer sees - the Driver
// box and the label on the big button. The two the user must MATCH are identical in
// both states. See the caption in setup.cpp, which is asserted by installer/verify.
#define IDR_ZADIG_SHOT_PNG      111

namespace bcdsetup {

// Every path buffer in this program is this size. Win32 still enforces MAX_PATH
// (260) for the plain (non "\\?\") APIs we use, so a longer path fails at the
// API rather than being silently truncated here - which is the failure mode we
// want, because truncation would build a path that points somewhere else.
const DWORD kPathMax = 1024;

// ---------------------------------------------------------------------------
// Names and locations. Changing any of these changes where the product lives.
// ---------------------------------------------------------------------------

// The COM class id of the ASIO driver. It MUST match IID_ASIO_DRIVER in
// native/bcdasio/bcdasio.cpp. Changing it orphans every host application that
// already has the driver selected.
extern const wchar_t* const kAsioClsid;

// The name under HKLM\SOFTWARE\ASIO. Must match the szregname passed to
// RegisterAsioDriver() in native/bcdasio/bcdasio.cpp.
extern const wchar_t* const kAsioRegName;

extern const wchar_t* const kProductName;
extern const wchar_t* const kInstallDirName;    // under %ProgramFiles%
extern const wchar_t* const kAsioDllFileName;
extern const wchar_t* const kUninstallExeName;
extern const wchar_t* const kManifestFileName;

// The control service lives in the user profile and starts at sign-in from a
// shortcut in the Startup folder. Both the folder and the shortcut name are the
// ones already in use, so re-running the setup on a working machine is a no-op
// instead of a second parallel installation.
extern const wchar_t* const kBridgeDirName;     // under %LOCALAPPDATA%
extern const wchar_t* const kBridgeExeFileName;
extern const wchar_t* const kShortcutFileName;
extern const wchar_t* const kLogFileName;

extern const wchar_t* const kZadigDownloadPage;

// The registry path the driver itself reads to find the device. It must stay
// equal to kEnumKey in native/bcdasio/usbdev.cpp: if the two disagree, the
// installer reports a working binding that the driver cannot use, or the other
// way round.
extern const wchar_t* const kUsbEnumKey;

// The same key split at the last backslash, so that the SIBLING functions of the
// same device can be enumerated: the parent that holds one subkey per USB function,
// and the prefix every function of this device shares. Zadig writes the interface
// number as two hex digits after it - MI_00, MI_01 - and binding the wrong one of
// those is the specific mistake page 2 exists to catch.
//
// THE THREE MUST AGREE, AND THAT IS MEASURED RATHER THAN TRUSTED: installer/verify
// rebuilds kUsbEnumKey out of these two and function 00 and compares the strings, so
// a future edit to one of them cannot quietly leave the sibling search looking at a
// different device from the one the binding check reads.
extern const wchar_t* const kUsbEnumParentKey;
extern const wchar_t* const kUsbFunctionPrefix;

// ---------------------------------------------------------------------------
// WHICH MIXER THIS IS, AND WHY THE INSTALLER RE-DECLARES WHAT THE DRIVER ALREADY
// KNOWS.
//
// *** THE OBVIOUS ARRANGEMENT IS NOT AVAILABLE, AND IT WAS MEASURED RATHER THAN
//     ASSUMED. *** native\bcdasio\usbdev.h declares DeviceProfile and
// matchedProfile(), and reading the profile table straight out of the driver would
// be one definition instead of two. The installer cannot see them: installer\
// build.bat compiles five product units - check, uninstall, setup, common, gui -
// and not one of them includes anything from native\, so reaching those symbols
// means adding units or include paths and moving the six units build.bat strict is
// pinned to.
//
// So model knowledge stays exactly where this file already keeps it. kUsbEnumKey
// above already hard-codes VID_1397 and PID_00BF, and kUsbFunctionPrefix repeats
// them; these four values are the same facts said in a form a screen can offer as a
// CHOICE, and the second model goes beside the first by the same mechanism.
//
// *** AND THE DRIFT IS WHAT IS MEASURED, WHICH IS THE HALF THAT MAKES TWO COPIES
//     ACCEPTABLE. *** installer\verify asserts these against
// native\bcdasio\usbdev.cpp BY READING IT AS TEXT - a compiled constant on one side
// and source the product never links on the other, which is two genuinely
// independent sides rather than one value read twice. Reading source text in the
// harness is already how the exe mode's freshness check works. It also rebuilds
// kUsbFunctionPrefix out of modelVid(0) and modelPid(0), so the detection key and
// this table cannot drift apart inside this file either.
//
// *** provenOnHardware IS NOT DECORATION: IT IS THE FACT BEHIND THE WORDING. *** The
// BCD3000 is validated on hardware and the BCD2000 has never once been run - nobody
// on this project owns one - and the screen that offers the choice has to say so. A
// flag and a sentence that can drift apart is how a screen comes to call something
// unsupported for a year after it was proven, so installer\verify asserts that the
// model this program hedges about IS the model the driver's table calls unproven.
// The day somebody proves a BCD2000 and flips that flag in usbdev.cpp, the screen
// fails loudly instead of ageing quietly.
//
// INDICES AND NOT AN ENUM, because bcdsetup::selectedModel() answers with one and
// the harness loops over them. They are the same order as the driver's kProfiles.
// ---------------------------------------------------------------------------
const int kModelCount   = 2;
const int kModelBcd3000 = 0;
const int kModelBcd2000 = 1;

// All four return a safe answer for an index out of range rather than reading past
// the table: a null name, a zero id, and "not proven", which is the answer that
// promises least.
const wchar_t* modelName(int index);
unsigned short modelVid(int index);
unsigned short modelPid(int index);
bool           modelProvenOnHardware(int index);

// ---------------------------------------------------------------------------
// Output. Every line goes to the console and, once logOpen() succeeded, to the
// log file as UTF-8. Nothing this program does is silent.
// ---------------------------------------------------------------------------

// Appends to the given file. Failure is reported once and is not fatal: a
// missing log must not stop an install.
void logOpen(const wchar_t* fullPath);
void logClose();

// Whether a log file is open RIGHT NOW.
//
// It exists for one reason and it is worth stating, because a reader will otherwise
// wonder what a program that always logs needs this for: the /preview modes of the
// setup and the uninstaller promise to write NOTHING, and the log file is the write
// they are most likely to make by accident. It was made by accident once, in the first
// implementation of BCD3000Setup.exe's /preview, and caught before it shipped. This
// turns "the log was not opened" from a claim about the order of two statements into
// something installer/verify can read back after calling the real prepare().
bool logIsOpen();

void say(const wchar_t* fmt, ...);      // plain line
void sayInfo(const wchar_t* fmt, ...);  // "[info] "
void sayOk(const wchar_t* fmt, ...);    // "[ ok ] "
void sayWarn(const wchar_t* fmt, ...);  // "[warn] "  counts
void sayFail(const wchar_t* fmt, ...);  // "[FAIL] "  counts
void sayBlank();

int  warningCount();
int  failureCount();

// ---------------------------------------------------------------------------
// THE SEAM BETWEEN THE LOGIC AND WHATEVER IS SHOWING IT
//
// Everything above and below this block decides things and says what it decided.
// It does not know whether anybody is looking at a console or at a window, and
// it must not learn: the console output is what an automated run reads back to
// verify an install, so the decisions have to be identical in both modes and the
// text has to come out of one place.
//
// These four hooks are the whole of the window's access to that text. A hook is
// installed once, before any step runs, and is never called with a partial line.
// ---------------------------------------------------------------------------

// What kind of line it is, so a window can mark it without parsing the prefix.
enum LineKind {
    kLinePlain = 0,
    kLineInfo  = 1,
    kLineOk    = 2,
    kLineWarn  = 3,
    kLineFail  = 4
};

// Called for every complete line, on whatever thread produced it - which in
// window mode is the WORKER thread. An implementation therefore may not touch a
// window; it has to post the text and return.
typedef void (*LineSink)(LineKind kind, const wchar_t* prefix, const wchar_t* body);
void setLineSink(LineSink sink);

// What is installed now, so that a caller can chain a sink of its own in front of
// it and put it back afterwards. There is exactly one user: page 2's text pane,
// which has to hold the same bytes the console and the log file got. Without a
// getter the only way to do that is a second copy of the words, and two copies of a
// paragraph drift - which is the defect this whole seam exists to make impossible.
LineSink lineSink();

// Window mode turns the console off after it has hidden it, so that a write to a
// console this process has already freed cannot happen at all. The log file is
// unaffected: it is written in both modes.
void setConsoleEcho(bool on);

// Where askYesNo() gets its answer from when there is no console to read. The
// hook is called on the calling thread and must be synchronous; askYesNo() logs
// the question and the answer in exactly the same words either way, so a log
// from a window run and a log from a console run compare line for line.
typedef bool (*AskHook)(const wchar_t* question);
void setAskHook(AskHook hook);

// Human readable text for a Win32 error code. Returns a pointer to a static
// per-call buffer; copy it if you need to keep it. Not thread safe, and this
// program is single threaded on purpose.
const wchar_t* winErrText(DWORD err);

// Waits for Enter only when this process owns its console window, which is the
// case when the setup was started by double click and Windows created a fresh
// console for the elevated process. Without this the window closes before
// anybody can read the summary.
void pauseIfWeOwnTheConsole();

// Reads one line from stdin and returns true when the user typed y or Y.
// Returns false on end of input, so a redirected stdin never means "yes".
bool askYesNo(const wchar_t* question);

// ---------------------------------------------------------------------------
// Paths
//
// THE PER USER FOLDERS ARE RESOLVED AGAINST THE ACCOUNT THAT OWNS THE DESKTOP,
// not against the account this process runs as. The two are the same for the
// ordinary "Yes" elevation and different when Windows asked for an
// administrator's password: in that case the process token belongs to the
// administrator, and resolving %LOCALAPPDATA% and the Startup folder from it
// would put the control service in a profile that never signs in, while
// reporting the running service of the real user as if it were ours.
//
// The token is taken from explorer.exe in this session, the same process
// detectInteractiveAccount() asks. When there is no explorer to ask, or opening
// its token is denied, these fall back to this process's own profile and
// AccountInfo::checked stays false so that callers can say so out loud.
//
// getProgramFilesDir() is machine wide and deliberately does NOT use that token.
// ---------------------------------------------------------------------------
bool getProgramFilesDir(wchar_t* out, DWORD count);
bool getLocalAppDataDir(wchar_t* out, DWORD count);
bool getStartupDir(wchar_t* out, DWORD count);
bool joinPath(wchar_t* out, DWORD count, const wchar_t* left, const wchar_t* right);

bool installDirPath(wchar_t* out, DWORD count);    // %ProgramFiles%\<kInstallDirName>
bool asioDllPath(wchar_t* out, DWORD count);
bool uninstallExePath(wchar_t* out, DWORD count);
bool manifestPath(wchar_t* out, DWORD count);
bool bridgeDirPath(wchar_t* out, DWORD count);     // %LOCALAPPDATA%\<kBridgeDirName>
bool bridgeExePath(wchar_t* out, DWORD count);
bool shortcutPath(wchar_t* out, DWORD count);      // Startup\<kShortcutFileName>
bool logFilePath(wchar_t* out, DWORD count);       // inside the bridge folder

// ---------------------------------------------------------------------------
// Filesystem
// ---------------------------------------------------------------------------
bool fileExists(const wchar_t* path);
bool dirExists(const wchar_t* path);

// Creates one directory level. Succeeds when it already exists. Reports the
// real error otherwise, instead of collapsing "already there" and "denied"
// into one answer.
bool ensureDir(const wchar_t* path, DWORD* winErr);

// True when the file exists and its bytes are exactly the given bytes. This is
// what makes the setup idempotent: an unchanged payload is not rewritten, so a
// second run does not touch a file that a running program may be using.
bool fileHasContent(const wchar_t* path, const void* data, DWORD size);

// Writes to a temporary name in the same directory and then renames over the
// target, so an interrupted write can never leave a half written driver behind.
bool writeFileAtomic(const wchar_t* path, const void* data, DWORD size, DWORD* winErr);

// Writes the bytes to exactly this path, creating or truncating it, with no
// temporary name and no rename. Used when the caller has to INSPECT the file
// before letting it take the place of something that already works.
bool writeFileDirect(const wchar_t* path, const void* data, DWORD size, DWORD* winErr);

bool loadPayload(int resourceId, const void** data, DWORD* size);

// Checks a file before it is allowed to replace a driver that works.
//
// The order the setup used to run in was: write over the registered driver, then
// try to load it. A corrupt or wrong architecture payload replaced a working
// driver and only then failed, which is the one outcome an installer must never
// produce. This is the check that goes in between.
//
// It requires an x64 PE DLL that Windows actually loads and that exports
// DllRegisterServer - which is the entry point the setup is about to call, so a
// file that lacks it is not this driver whatever else it may be. *whyNot points
// at a static English explanation on failure; do not free it.
bool checkDriverFile(const wchar_t* path, const wchar_t** whyNot, DWORD* winErr);

// Writes text as UTF-8 with no byte order mark. Used for the install manifest,
// which is meant to be read by a human and by the uninstaller.
bool writeTextFileUtf8(const wchar_t* path, const wchar_t* text, DWORD* winErr);

// ---------------------------------------------------------------------------
// The install manifest
//
// Written by the setup before it changes anything, read by the setup on a second
// run and by the uninstaller. It is INFORMATION ONLY: every path the uninstaller
// deletes is worked out from the Windows folders exactly the way the setup worked
// it out, so a tampered manifest cannot redirect a deletion.
// ---------------------------------------------------------------------------

// Reads one "key=value" line. Matches only at the start of a line, so a value
// that contains the key name cannot be mistaken for the key.
bool readManifestValue(const wchar_t* key, wchar_t* out, DWORD count);

// The same search, on text that is already in memory. Split out so that the part
// with the decisions in it can be exercised without a manifest on disk: the real
// one lives in %ProgramFiles%, and a test that needs to read or write there is a
// test nobody can run on a machine that is in use.
bool findManifestValueIn(const wchar_t* text, const wchar_t* key,
                         wchar_t* out, DWORD count);

// True when a raw previous_asio_registration value names a driver and not the
// "there was nothing registered" placeholder. Empty is not a recording either:
// that distinction is the whole point, because an empty answer from a second
// install run must never be written over what the first run recorded.
bool isRecordedRegistration(const wchar_t* value);

// The ASIO registration that was in place BEFORE this product was installed.
//
// This is the one value in the manifest that cannot be worked out again later,
// and the only instruction that puts a machine back the way it was found. Both
// the setup and the uninstaller go through this function so that the "(none)"
// placeholder is spelled in exactly one place. Returns false when there is no
// manifest, no such line, or the line says there was nothing registered.
bool readRecordedPreviousRegistration(wchar_t* out, DWORD count);

// That placeholder, for the one place that has to write it.
const wchar_t* manifestNoPreviousText();

// Terminates every running instance of the control service and waits for them
// to go away.
//
// *** This destroys the virtual MIDI port. A DJ application that is open at that
// moment will not find the controller again until it is restarted - measured
// three times on the hardware, see the header of native/bcdasio/midibridge.h.
// Never do this as a routine step. ***
//
// Returns true ONLY when a fresh snapshot taken afterwards finds no instance
// left. A process it could not open, a process that outlived the wait, and a
// snapshot that could not be walked at all are all failures: the caller uses
// this answer to decide whether the device and the service's own file are free,
// and "I terminated nothing" is not the same statement as "nothing is running".
bool stopBridge(int* terminated, DWORD* winErr);

// ---------------------------------------------------------------------------
// Detection. All of it read only.
// ---------------------------------------------------------------------------

struct AsioRegInfo {
    bool    clsidKeyPresent;         // HKCR\CLSID\{clsid}
    bool    inprocPresent;           // ...\InprocServer32 default value
    wchar_t inprocPath[kPathMax];    // the DLL a host application would load
    bool    asioNameKeyPresent;      // HKLM\SOFTWARE\ASIO\<name>
    wchar_t asioNameClsid[64];
    bool    clsidMatches;            // that key points at our clsid
    bool    perUserShadowPresent;    // HKCU\Software\Classes\CLSID\{clsid}
};
void detectAsioRegistration(AsioRegInfo* out);

// Does the Service Control Manager know a service by that name? A pure read: it opens
// the SCM for CONNECT and the service for QUERY_STATUS, both of which an ordinary user
// gets, and closes both. It starts nothing, changes nothing and loads nothing.
//
// It is PUBLISHED rather than static because the verification harness has to be able
// to call it with a name that must exist and a name that cannot, which is the only way
// to show that it can answer both.
bool serviceIsRegistered(const wchar_t* serviceName);

// ---------------------------------------------------------------------------
// WINDOWS MIDI SERVICES - THE THING THE MIDI PORT NOW STANDS ON, AND THE ONE
// WINDOWS DEFECT A USER COULD NOT POSSIBLY GUESS AT.
//
// The product's virtual MIDI port is created through Windows MIDI Services,
// which is IN-BOX. There is nothing here for anybody to install, which is why
// this screen stopped offering an install and started reporting a reading.
//
// *** WHAT THIS READS, AND WHY IT IS ONLY THESE. *** Three cheap reads, none of
// which starts anything, loads anybody's library or creates a port:
//
//   1. does the SCM know a service called midisrv        - serviceIsRegistered()
//   2. is Midi2.VirtualMidiTransport.dll in the system directory, and what
//      version resource does it carry
//   3. what version resource does midisrv.exe carry
//
// *** AND WHY IT NEVER CREATES A PORT TO FIND OUT. *** Creating one is the one
// act that would answer "will the port work", and it is forbidden here for a
// measured reason: under the defect below, the FIRST virtual port of a boot is
// the only one that can be created, so an installer that created a port to test
// it would spend the machine's one port on a test and leave the control service
// with none until the next reboot. Every sentence this program prints about the
// MIDI port therefore says what was LOOKED AT and never that a port will work.
//
// ---------------------------------------------------------------------------
// *** THE DEFECT. microsoft/MIDI ISSUE #1047. ***
//
// "Virtual endpoints unusable on build 26100.8875 (KB5101650)", open since
// 2026-07-16, labels bug and needs-investigation. Microsoft acknowledged it on
// 2026-07-17 with internal ADO bug 63135869 and has given NO TIMELINE.
//
// Measured behaviour, reproduced on the owner's machine across two sessions and
// six reboots: after the FIRST virtual MIDI port of a boot is CLOSED, every
// later creation attempt fails until the machine is restarted.
//
// The defect is in Midi2.VirtualMidiTransport.dll, which ships INSIDE WINDOWS
// and is not a file this product redistributes. So the fix arrives by Windows
// Update and cures the product without republishing anything - which is the
// whole reason this program reports the build rather than offering a download.
// ---------------------------------------------------------------------------

// The service the whole stack hangs off. Named once, here, because two programs
// in this folder ask about it and a second spelling would be a second subject.
extern const wchar_t* const kMidiServiceName;      // L"midisrv"
extern const wchar_t* const kMidiServiceExeName;   // L"midisrv.exe"
extern const wchar_t* const kMidiTransportDllName; // L"Midi2.VirtualMidiTransport.dll"

// ---------------------------------------------------------------------------
// *** THE KNOWN-BAD LIST. THIS IS THE ONE PLACE THAT CHANGES WHEN MICROSOFT
//     SHIPS THE FIX FOR microsoft/MIDI #1047. ***
//
// It is a RANGE PER SERVICING BRANCH and not a bare pair of magic numbers, and
// the reason it is a range rather than the two literal builds the issue's title
// names is a measurement taken on the owner's own machine on 2026-08-02:
//
//   midisrv.exe                    10.0.26100.8972   <- past .8875
//   Midi2.VirtualMidiTransport.dll  1.0.15.0
//
// and the defect reproduced on that machine on 2026-08-01 at 19:1x, three days
// after KB5101711/KB5101684 took it to .897x. So an explicit list of exactly
// {26100.8875, 26200.8875} would answer "NOT known bad" on the very machine
// where the defect was reproduced. KB5101650 (.8875) is where it ARRIVED; no
// fix has shipped, so the range has no upper bound yet.
//
// firstFixedRevision is 0 while nothing has shipped, and 0 means "open ended".
// *** WHEN MICROSOFT SHIPS THE FIX: put the first fixed revision of each branch
//     in the table below and nothing else in this program has to move. *** If
// the fix arrives instead as a new Midi2.VirtualMidiTransport.dll component
// version, add a branch here for that and say so in this block; that DLL's own
// version (1.0.15.0) is the Windows MIDI Services component version and is NOT
// comparable with a Windows build number - measured, and the reason the build
// test is taken off the service binary rather than off the transport DLL.
// ---------------------------------------------------------------------------
struct KnownBadMidiBuild {
    DWORD build;               // the servicing branch, e.g. 26100
    DWORD firstBadRevision;    // the revision the defect arrived in
    DWORD firstFixedRevision;  // the first revision that has it fixed; 0 = none yet
};
extern const KnownBadMidiBuild kKnownBadMidiBuilds[];
extern const int               kKnownBadMidiBuildCount;

// True when build.revision falls inside one of the ranges above. A PURE function
// of two numbers, published so that the harness can drive it either way without
// owning a machine on the affected build.
bool midiBuildIsKnownBad(DWORD build, DWORD revision);

// The same decision over ANY table, which is what makes the branch that does not
// exist yet testable.
//
// *** THIS SPLIT IS NOT GENERALITY FOR ITS OWN SAKE. *** Every row in the shipped
// table has firstFixedRevision == 0 today, so the "Microsoft shipped the fix and
// this revision is past it" branch has no input that can reach it and would be
// dead code nothing could prove - on the ONE function whose whole purpose is to
// be edited the day that fix lands. Handed a table, the harness drives all four
// outcomes today; midiBuildIsKnownBad() is then one line over the real one, and
// installer/verify asserts the real one's contents separately so that the
// harness is not left testing a fixture of its own making.
bool midiBuildIsKnownBadIn(const KnownBadMidiBuild* table, int count,
                           DWORD build, DWORD revision);

// The version resource of a file, as its four numbers. A pure read: it opens
// nothing but the file's own resource section, runs no code out of it, and does
// not load it as a module.
//
// PUBLISHED for the same recorded reason serviceIsRegistered() is: the harness
// has to be able to call it with a file that must have a version resource and a
// path that cannot exist, which is the only way to show it can answer both.
bool fileVersionNumbers(const wchar_t* path, DWORD out[4], DWORD* winErr);

struct WinMidiInfo {
    bool  serviceRegistered;    // the SCM knows kMidiServiceName
    bool  transportPresent;     // the transport DLL is in the system directory
    bool  transportVersionRead; // ...and its version resource answered
    DWORD transportVersion[4];  // 1.0.15.0 - the COMPONENT version, not a build
    bool  serviceVersionRead;   // midisrv.exe's version resource answered
    DWORD serviceVersion[4];    // 10.0.26100.8972 - major.minor.BUILD.REVISION
    DWORD lastError;            // the first read that failed, 0 when none did
};
void detectWindowsMidi(WinMidiInfo* out);

// The three states screen 3 has, and the ONLY three. Anything that is not a
// complete, decidable reading is kWinMidiUnread - which prints the numbers and
// invents no cause, because a cause this program did not measure is a guess.
enum WinMidiState {
    kWinMidiReady    = 0,  // service there, transport there, build not known bad
    kWinMidiKnownBad = 1,  // all of that, and the build IS on the list above
    kWinMidiUnread   = 2   // anything else
};

// Pure function of the readings. Decides nothing else and reads nothing else, so
// the harness can hand it an invented machine and read the answer back.
WinMidiState classifyWindowsMidi(const WinMidiInfo* w);

struct WinUsbInfo {
    bool    enumKeyPresent;      // the device has been plugged in at least once
    bool    guidPresent;         // a WinUSB interface guid is recorded for it
    wchar_t guid[64];
    bool    interfacePresentNow; // and the interface is enumerated right now

    // When guidPresent is false: which OTHER MI_ function of this device carries a
    // WinUSB interface guid, or -1 when none does.
    //
    // *** THIS IS THE DIFFERENCE BETWEEN TWO FACTS THAT LOOK IDENTICAL ON THE PAGE.
    // "You have not run Zadig yet" and "you ran Zadig on the wrong line of its list"
    // both leave MI_00 without a guid and both paint the row red. Only the second one
    // can be repaired by telling somebody which line to pick, and until this field
    // existed the page could not tell them apart, so it said nothing about either.
    //
    // *** WHY EVERY READER ASKS FOR > 0 AND NOT >= 0. *** MachineState is
    // ZeroMemory'd in a dozen places in this tree and in the harness, so an
    // uninitialised copy of this struct carries 0 - and 0 would read as "interface 0
    // is bound", which is precisely what guidPresent being false has already denied.
    // Zero is therefore not a usable answer in this field's own terms, and reading it
    // as "no sibling" is what stops a zeroed state from inventing a wrong binding
    // nobody measured. installer/verify asserts that directly.
    int     guidOnOtherFunction;

    DWORD   lastError;
};
void detectWinUsbBinding(WinUsbInfo* out);

struct BridgeInfo {
    bool    running;
    int     instanceCount;       // a one file build shows up as two processes
    DWORD   firstPid;
    wchar_t imagePath[kPathMax];
};
void detectBridge(BridgeInfo* out);

bool isElevated();

// ---------------------------------------------------------------------------
// WINGET, THE GENERIC READING BEHIND WHATEVER ACCELERATION THIS INSTALLER OFFERS
//
// This installer does not currently offer any winget accelerated install - the one
// screen that used to (the MIDI port screen) had that offer removed along with the
// third party detection it was built on. What is left is the plain reading of
// whether winget itself is usable, kept because it is generic and a future screen
// may want it again. IF A FUTURE OFFER IS BUILT ON TOP OF IT, it must keep the two
// properties that made the removed one honest, and either one
// got wrong turns a respectful arrangement into the one that was rejected.
//
//  1. WITH ITS INTERFACE. The command line may never carry /silent, /quiet,
//     --accept-package-agreements or anything else that suppresses the author's own
//     user interface, because his licence lives in the Burn theme's
//     EulaAcceptCheckbox and under /quiet it is NEVER SHOWN AT ALL. Running it
//     silently would move the duty to disclose that licence from him onto us.
//
//     *** THIS IS A RULE FOR WHOEVER BUILDS THE NEXT OFFER, AND TODAY NOTHING
//         ENFORCES IT, WHICH IS THE HONEST STATE. *** It used to say the rule "is
//     asserted by the harness on the string buildWingetInstallCommand() returns,
//     not trusted to a comment". That sentence outlived what it described: this
//     program builds no winget command line at all any more, the builder is
//     deleted, and the harness assertion went with it. A comment claiming a live
//     guarantee where there is only an instruction is the same defect class this
//     project keeps removing from its user-facing text, so it is corrected here
//     rather than left reading as though a shipped command line were being
//     checked. Whoever adds the offer back adds the builder, the assertion on its
//     output, and this sentence's teeth, in the same change.
//
//  2. UNELEVATED, IN THE DESKTOP OWNER'S SESSION. See launchUnelevated().
// ---------------------------------------------------------------------------
enum WingetState {
    kWingetUsable  = 0,   // winget --version answered
    kWingetMissing = 1,   // the execution alias does not resolve
    kWingetBlocked = 2    // present, but it refused (policy, or non-zero exit)
};
struct WingetInfo {
    WingetState state;
    wchar_t     version[64];
    DWORD       lastError;
};

// Runs "winget --version" with a deadline and reads what it printed. NOT "where":
// winget is an execution alias, so a file that exists proves nothing about whether
// it runs, and a file that is missing from one PATH proves nothing either.
//
// The documented "unavailable until the user has signed in once" caveat does not
// apply to this program: it is interactive by definition, so somebody is signed in.
// What does apply is the DisableAppInstaller policy, Windows 10 before 1809, and
// LTSC images with no Store - and those are the three that kWingetBlocked and
// kWingetMissing exist to tell apart from each other.
void detectWinget(WingetInfo* out);

// *** buildWingetInstallCommand() WAS DECLARED HERE AND IS DELETED. *** It was the
// one place a winget command line was built. Its only production caller was the
// accelerated offer on the MIDI port screen, which went with the third party
// detection it was built on; no task left in this plan installs anything through a
// package manager. See the block over its former definition in common.cpp for the
// full reasoning, including why this one goes while serviceIsRegistered() stays.
// detectWinget() below is NOT affected - it still has production readers, because
// what winget can do on this machine is a fact the uninstaller's /preview and the
// checker both report.

// Launches cmdline in the session of the account that owns the desktop and WITHOUT
// elevation, so that a child which needs administrator rights raises its OWN UAC
// prompt naming its OWN publisher. False (with *err set) when the token cannot be
// had; the caller then falls back to opening a page.
//
// *** WHY CreateProcessWithTokenW AND NOT CreateProcessAsUser. *** They look
// interchangeable and are not. CreateProcessAsUser needs SE_ASSIGNPRIMARYTOKEN,
// which an elevated administrator normally does NOT hold; CreateProcessWithTokenW
// needs SE_IMPERSONATE_NAME, which an elevated administrator does. Picking the
// first is how this path fails on every machine with a privilege error.
//
// *** AND WHY UNELEVATED IS THE POINT RATHER THAN A DETAIL. *** A child inherits
// its parent's token - Microsoft's own documentation calls the parent/child
// relationship the one exception to elevation being asked for again. Launched from
// this elevated process a third party installer would run elevated silently and the
// user would be shown nothing. Launched unelevated it raises its own prompt, and
// that prompt names its own publisher from its own certificate. We do not
// substitute for the system's disclosure; we hand the disclosure back to the system.
//
// *procOut is a handle the caller must close. It is only written on success.
bool launchUnelevated(const wchar_t* cmdline, DWORD* err, HANDLE* procOut);

// True when the desktop owner's token was opened with the rights
// CreateProcessWithTokenW needs. Read once, like the token itself, so that the page
// can offer what it can actually do instead of finding out after the press.
bool interactiveTokenCanLaunch();

// Opens a page as the interactive user. The bottom rung of the ladder, and the
// answer to every condition above it.
//
// The url is checked before it is put on a command line: only plain ASCII, no
// quotes, no spaces, no control characters, and it must begin http:// or https://.
// Every url this program opens is a literal in this file, so the check can never
// reject a legitimate one - which is exactly when a boundary check is worth having.
bool openPageInBrowser(const wchar_t* url, DWORD* err);

// That check, split out so it can be exercised without opening anything.
bool isSafeUrlForCommandLine(const wchar_t* url);

struct AccountInfo {
    bool    checked;             // false when the shell owner could not be read
    bool    matched;
    wchar_t tokenAccount[256];   // who this process runs as
    wchar_t shellAccount[256];   // who owns the interactive desktop
};
void detectInteractiveAccount(AccountInfo* out);

// ---------------------------------------------------------------------------
// Which Windows this is.
//
// WHY IT IS GATHERED HERE AND NOT ASKED FOR WHERE IT IS USED. The page that shows
// it is built by fillPreflightRows(), which is a pure presentation function of an
// already gathered state: it reads nothing and decides nothing, which is what lets
// installer/verify render every branch of it from an invented machine. A version
// read inside the row builder would make one row un-invented and therefore
// untestable, and the harness would be measuring this machine again.
//
// WHY GetVersionEx REPORTS THE TRUTH HERE. It lies - reporting 6.2 - only to a
// process whose manifest does not declare the operating systems it was built for.
// admin.manifest ALREADY declares supportedOS {8e0f7a12-...}, which is Windows 10
// and 11, and so does the harness's own manifest. RtlGetVersion is therefore not
// needed and the manifest is not touched: its comment block spends thirty lines
// justifying the DPI declarations, and editing it reopens that for no gain.
//
// AND WHY THE TEST IS A BUILD NUMBER. Windows 11 reports major 10, minor 0 - the
// same pair as Windows 10 - and is told apart only by the build. 22000 is the first
// Windows 11 build. A machine older than Windows 10 is not Windows 10 either, and
// gets no row.
// ---------------------------------------------------------------------------
const DWORD kFirstWindows11Build = 22000;

struct OsInfo {
    bool  read;          // false when Windows refused to answer at all
    DWORD major;
    DWORD minor;
    DWORD build;
    bool  isWindows10;   // 10.0 with a build below the first Windows 11 build
};
void detectWindowsVersion(OsInfo* out);

// ---------------------------------------------------------------------------
// The whole machine state in one read only pass, plus a printer for it.
//
// The setup and BCD3000Check.exe call exactly these two functions, so the report
// a user sends with a support request is the same report the setup acted on.
// ---------------------------------------------------------------------------
struct MachineState {
    bool        pathsResolved;
    bool        elevated;
    AccountInfo account;
    AsioRegInfo asio;
    WinUsbInfo  usb;
    BridgeInfo  bridge;
    OsInfo      os;

    // WHAT WINDOWS MIDI SERVICES LOOKS LIKE ON THIS MACHINE. Here, and not
    // computed where screen 3 is built, for the same reason the Windows version
    // and the winget reading are here: buildScreens() has to be a PURE function
    // of a state, or installer/verify cannot render the screen for an invented
    // machine and every capture would photograph whatever build the harness ran
    // on. See the block over WinMidiInfo for what it reads and what it refuses
    // to read.
    WinMidiInfo winMidi;

    // WHY THESE TWO LIVE IN THE STATE AND NOT WHERE THE BUTTON IS DECIDED. Both are
    // measurements of this machine, and the offer page 2 makes has to be a PURE
    // function of a state - otherwise installer/verify cannot render the offer for
    // an invented machine, and every capture would show whatever winget happens to
    // be on the machine the harness ran on. That is the same rule the Windows
    // version already obeys, for the same reason, and the block over OsInfo spells
    // it out.
    WingetInfo  winget;
    // True when the desktop owner's token can be duplicated for
    // CreateProcessWithTokenW. False is not a failure: it is the condition that
    // turns the offer into "open the page".
    bool        tokenCanLaunch;

    wchar_t installDir[kPathMax];
    wchar_t dllTarget[kPathMax];
    wchar_t bridgeTarget[kPathMax];
    wchar_t shortcutFile[kPathMax];
    wchar_t shortcutPointsAt[kPathMax];   // empty when there is no shortcut
    bool    shortcutPresent;

    // True when a registration exists and points at something other than
    // dllTarget. This is the single most consequential thing the setup changes,
    // so it gets its own flag rather than being recomputed at each use.
    bool    registeredElsewhere;
};

void gatherMachineState(MachineState* out);
void reportMachineState(const MachineState* s);

// ---------------------------------------------------------------------------
// Shortcut
// ---------------------------------------------------------------------------
bool readShortcutTarget(const wchar_t* lnkPath, wchar_t* target, DWORD count);
bool createShortcut(const wchar_t* lnkPath, const wchar_t* target,
                    const wchar_t* workingDir, const wchar_t* description,
                    HRESULT* hrOut);

// ---------------------------------------------------------------------------
// COM self registration of the driver
// ---------------------------------------------------------------------------

// Loads the DLL and calls its DllRegisterServer / DllUnregisterServer, which is
// what regsvr32 does. Done in process so that the HRESULT is available instead
// of only an exit code.
//
// Note for whoever reads a support log: the driver puts up a message box of its
// own when registration fails, so a failure here can block until somebody
// clicks OK.
bool callDllRegisterServer(const wchar_t* dllPath, HRESULT* hrOut, DWORD* winErr);
bool callDllUnregisterServer(const wchar_t* dllPath, HRESULT* hrOut, DWORD* winErr);

// Last resort cleanup for the uninstaller, used when the DLL is already gone
// and there is nothing left to call DllUnregisterServer on.
//
// It deletes a key only after checking that the key belongs to this product:
// the class id entry only when its InprocServer32 equals expectedDllPath, and
// the ASIO entry only when it points at our class id. HKLM\SOFTWARE\ASIO itself
// is never removed - other ASIO drivers live there.
//
// The class id entry is read and deleted IN THE SAME HIVE, once for
// HKLM\SOFTWARE\Classes and once for HKCU\Software\Classes.
// HKEY_CLASSES_ROOT is the merged view of the two, so validating through it and
// then deleting from the machine hive checks one key and destroys another
// whenever a per user shadow exists - and the shadow, which is the entry that
// actually wins for that user, would be left behind.
bool deleteAsioRegistryKeys(const wchar_t* expectedDllPath, bool* removedClsid,
                            bool* removedAsioName);

}
