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

// Third party dependency. Not redistributed by us and not installed by us.
extern const wchar_t* const kTeVmDllName;
extern const wchar_t* const kTeVmDownloadPage;
extern const wchar_t* const kZadigDownloadPage;

// The registry path the driver itself reads to find the device. It must stay
// equal to kEnumKey in native/bcdasio/usbdev.cpp: if the two disagree, the
// installer reports a working binding that the driver cannot use, or the other
// way round.
extern const wchar_t* const kUsbEnumKey;

// ---------------------------------------------------------------------------
// Output. Every line goes to the console and, once logOpen() succeeded, to the
// log file as UTF-8. Nothing this program does is silent.
// ---------------------------------------------------------------------------

// Appends to the given file. Failure is reported once and is not fatal: a
// missing log must not stop an install.
void logOpen(const wchar_t* fullPath);
void logClose();

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

enum TeVmState {
    kTeVmMissing = 0,
    kTeVmPresent = 1,
    // Present in the real system directory but absent from the literal path the
    // control service hard codes. Only happens when Windows is not on C:.
    kTeVmNotWhereTheServiceLooks = 2
};
struct TeVmInfo {
    TeVmState state;
    wchar_t   systemDirPath[kPathMax];
    wchar_t   hardCodedPath[kPathMax];
};
void detectTeVirtualMidi(TeVmInfo* out);

struct WinUsbInfo {
    bool    enumKeyPresent;      // the device has been plugged in at least once
    bool    guidPresent;         // a WinUSB interface guid is recorded for it
    wchar_t guid[64];
    bool    interfacePresentNow; // and the interface is enumerated right now
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

struct AccountInfo {
    bool    checked;             // false when the shell owner could not be read
    bool    matched;
    wchar_t tokenAccount[256];   // who this process runs as
    wchar_t shellAccount[256];   // who owns the interactive desktop
};
void detectInteractiveAccount(AccountInfo* out);

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
    TeVmInfo    tevm;
    WinUsbInfo  usb;
    BridgeInfo  bridge;

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
