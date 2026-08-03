#include "common.h"

#include <shlobj.h>
// WIN32_LEAN_AND_MEAN keeps windows.h from pulling this in, and shlobj.h does not
// bring it either. ShellExecuteW is the bottom rung of the fallback ladder.
#include <shellapi.h>
#include <objbase.h>
#include <setupapi.h>
#include <tlhelp32.h>
#include <sddl.h>
// GetFileVersionInfoW / VerQueryValueW / VS_FIXEDFILEINFO. WIN32_LEAN_AND_MEAN
// keeps windows.h from bringing this one, and version.lib is on the link line of
// build.bat and of verify\buildharness.bat - both, or the harness stops linking.
#include <winver.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>

namespace bcdsetup {

// ---------------------------------------------------------------------------
// Names and locations
// ---------------------------------------------------------------------------
const wchar_t* const kAsioClsid    = L"{B0D3000A-51E7-4C2B-9F3A-1234ABCD5678}";
const wchar_t* const kAsioRegName  = L"Behringer BCD3000";

const wchar_t* const kProductName     = L"Behringer BCD3000 ASIO driver";
const wchar_t* const kInstallDirName  = L"BCD3000 ASIO Driver";
const wchar_t* const kAsioDllFileName = L"BcdAsio.dll";
const wchar_t* const kUninstallExeName = L"BCD3000Uninstall.exe";
const wchar_t* const kManifestFileName = L"install-manifest.txt";

const wchar_t* const kBridgeDirName     = L"BCD3000Bridge";
const wchar_t* const kBridgeExeFileName = L"BCD3000Bridge.exe";
const wchar_t* const kShortcutFileName  = L"BCD3000 Bridge.lnk";
const wchar_t* const kLogFileName       = L"install.log";

const wchar_t* const kZadigDownloadPage = L"https://zadig.akeo.ie/";

// Windows MIDI Services - see the block over WinMidiInfo in common.h.
const wchar_t* const kMidiServiceName      = L"midisrv";
const wchar_t* const kMidiServiceExeName   = L"midisrv.exe";
const wchar_t* const kMidiTransportDllName = L"Midi2.VirtualMidiTransport.dll";

// *** THE ONE PLACE THAT CHANGES WHEN MICROSOFT SHIPS THE FIX FOR
//     microsoft/MIDI ISSUE #1047. *** The full reasoning, including the
// measurement that makes this a range instead of the issue title's two literal
// builds, is in the block over KnownBadMidiBuild in common.h. Read it before
// editing a number here.
//
//   26100  the Germanium servicing branch. What midisrv.exe on the owner's own
//          machine reports even though Windows brands itself 26200 - measured
//          2026-08-02, and the reason this row exists at all.
//   26200  the same revision under the 24H2/25H2 enablement branding, which is
//          how the issue's own title spells the second one. Nobody on this
//          project has a machine whose binaries carry it; kept because the
//          issue names it and a missing row is a silent "not affected".
const KnownBadMidiBuild kKnownBadMidiBuilds[] = {
    //  build   first bad (KB5101650)   first fixed (0 = no fix has shipped)
    {   26100,  8875,                   0 },
    {   26200,  8875,                   0 }
};
const int kKnownBadMidiBuildCount =
    (int)(sizeof(kKnownBadMidiBuilds) / sizeof(kKnownBadMidiBuilds[0]));

bool midiBuildIsKnownBadIn(const KnownBadMidiBuild* table, int count,
                           DWORD build, DWORD revision)
{
    if (!table)
        return false;
    for (int i = 0; i < count; i++) {
        const KnownBadMidiBuild* b = &table[i];
        if (b->build != build)
            continue;                       // a branch nobody has reported
        if (revision < b->firstBadRevision)
            return false;                   // older than KB5101650
        // 0 means no fix has shipped, so the range stays open at the top. This is
        // the branch that decides the answer on the owner's own machine: it is at
        // .8972, past .8875, and the defect still reproduces there.
        if (b->firstFixedRevision != 0 && revision >= b->firstFixedRevision)
            return false;                   // Microsoft shipped the fix
        return true;
    }
    return false;
}

bool midiBuildIsKnownBad(DWORD build, DWORD revision)
{
    return midiBuildIsKnownBadIn(kKnownBadMidiBuilds, kKnownBadMidiBuildCount,
                                 build, revision);
}

const wchar_t* const kUsbEnumKey =
    L"SYSTEM\\CurrentControlSet\\Enum\\USB\\VID_1397&PID_00BF&MI_00";

// The same path, split, so that the device's other functions can be looked at. The
// harness rebuilds the line above out of these two and refuses to pass if they have
// drifted apart.
const wchar_t* const kUsbEnumParentKey =
    L"SYSTEM\\CurrentControlSet\\Enum\\USB";
const wchar_t* const kUsbFunctionPrefix = L"VID_1397&PID_00BF&MI_";

// ---------------------------------------------------------------------------
// THE TWO MIXERS, AS VALUES A SCREEN CAN OFFER. See the block over these in
// common.h for why they are re-declared here instead of read out of
// native\bcdasio\usbdev.h, and for what installer\verify asserts them against.
//
// The order is the driver's kProfiles order, and the harness checks that too: an
// index is only meaningful if both tables mean the same thing by it.
//
// *** THE BCD2000's FLAG IS false AND THAT IS A FACT, NOT A HEDGE ABOUT A FACT. ***
// usbdev.cpp's own comment on that profile says it in words: EXPERIMENTAL, NUNCA
// RODOU, NINGUEM DESTE PROJETO TEM UMA. Every field of that profile but the vendor
// and product ids is a guess inherited from the BCD3000, each with a clean failure
// path. The screen that offers the choice says the true thing rather than the
// encouraging one, and the check that ties the two together is in installer\verify.
// ---------------------------------------------------------------------------
static const wchar_t* const kModelNames[kModelCount] = { L"BCD3000", L"BCD2000" };
static const unsigned short kModelVids[kModelCount]  = { 0x1397, 0x1397 };
static const unsigned short kModelPids[kModelCount]  = { 0x00BF, 0x00BD };
static const bool           kModelProven[kModelCount] = { true, false };

static bool modelIndexOk(int index)
{
    return index >= 0 && index < kModelCount;
}

const wchar_t* modelName(int index)
{
    return modelIndexOk(index) ? kModelNames[index] : 0;
}

unsigned short modelVid(int index)
{
    return modelIndexOk(index) ? kModelVids[index] : (unsigned short)0;
}

unsigned short modelPid(int index)
{
    return modelIndexOk(index) ? kModelPids[index] : (unsigned short)0;
}

// An index this table does not have is NOT proven, which is the answer that promises
// least. A default of true would make a bad index look like validated hardware.
bool modelProvenOnHardware(int index)
{
    return modelIndexOk(index) ? kModelProven[index] : false;
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------
static HANDLE g_log      = INVALID_HANDLE_VALUE;
static int    g_warnings = 0;
static int    g_failures = 0;

// The seam to whatever is showing the text. Plain function pointers and a plain
// bool: no object, no destructor, nothing that runs at unload.
static LineSink g_sink        = 0;
static AskHook  g_askHook     = 0;
static bool     g_consoleEcho = true;

void setLineSink(LineSink sink)   { g_sink = sink; }
LineSink lineSink()               { return g_sink; }
void setAskHook(AskHook hook)     { g_askHook = hook; }
void setConsoleEcho(bool on)      { g_consoleEcho = on; }

int warningCount() { return g_warnings; }
int failureCount() { return g_failures; }

// Writes a wide string to a handle as UTF-8. Used for the log file and for a
// redirected stdout, where WriteConsoleW does not apply.
static void writeUtf8(HANDLE h, const wchar_t* text)
{
    if (h == INVALID_HANDLE_VALUE || !text || !*text)
        return;
    int need = WideCharToMultiByte(CP_UTF8, 0, text, -1, 0, 0, 0, 0);
    if (need <= 1)
        return;
    char* buf = (char*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)need);
    if (!buf)
        return;
    if (WideCharToMultiByte(CP_UTF8, 0, text, -1, buf, need, 0, 0) > 0) {
        DWORD written = 0;
        WriteFile(h, buf, (DWORD)(need - 1), &written, 0);   // -1 drops the terminator
    }
    HeapFree(GetProcessHeap(), 0, buf);
}

// Console output only, exactly the text given, with no newline of its own.
static void toConsole(const wchar_t* text)
{
    if (!g_consoleEcho)
        return;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == INVALID_HANDLE_VALUE || out == 0)
        return;
    DWORD mode = 0;
    if (GetConsoleMode(out, &mode)) {
        DWORD written = 0;
        WriteConsoleW(out, text, (DWORD)wcslen(text), &written, 0);
    } else {
        writeUtf8(out, text);   // redirected to a file or a pipe
    }
}

// One complete line to every destination. The log line carries a timestamp and
// the console line does not: the console is being read live, and the log is the
// artifact that has to still make sense tomorrow.
//
// The sink is called LAST and with the line already complete, so a window can
// only ever receive whole lines, in order, and cannot influence what the console
// and the log got.
static void emitLine(LineKind kind, const wchar_t* prefix, const wchar_t* body)
{
    wchar_t line[4400];
    _snwprintf(line, 4300, L"%s%s\r\n", prefix, body);
    line[4300] = 0;
    toConsole(line);

    if (g_log != INVALID_HANDLE_VALUE) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t stamped[4500];
        _snwprintf(stamped, 4400, L"%02u:%02u:%02u.%03u %s%s\n",
                   st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, prefix, body);
        stamped[4400] = 0;
        writeUtf8(g_log, stamped);
    }

    if (g_sink)
        g_sink(kind, prefix, body);
}

static void emitPrefixed(LineKind kind, const wchar_t* prefix, const wchar_t* fmt,
                         va_list ap)
{
    wchar_t body[4096];
    _vsnwprintf(body, 4000, fmt, ap);
    body[4000] = 0;
    emitLine(kind, prefix, body);
}

void say(const wchar_t* fmt, ...)
{
    va_list ap; va_start(ap, fmt); emitPrefixed(kLinePlain, L"", fmt, ap); va_end(ap);
}
void sayInfo(const wchar_t* fmt, ...)
{
    va_list ap; va_start(ap, fmt); emitPrefixed(kLineInfo, L"[info] ", fmt, ap); va_end(ap);
}
void sayOk(const wchar_t* fmt, ...)
{
    va_list ap; va_start(ap, fmt); emitPrefixed(kLineOk, L"[ ok ] ", fmt, ap); va_end(ap);
}
void sayWarn(const wchar_t* fmt, ...)
{
    g_warnings++;
    va_list ap; va_start(ap, fmt); emitPrefixed(kLineWarn, L"[warn] ", fmt, ap); va_end(ap);
}
void sayFail(const wchar_t* fmt, ...)
{
    g_failures++;
    va_list ap; va_start(ap, fmt); emitPrefixed(kLineFail, L"[FAIL] ", fmt, ap); va_end(ap);
}
void sayBlank()
{
    say(L"");
}

void logOpen(const wchar_t* fullPath)
{
    logClose();
    HANDLE h = CreateFileW(fullPath, FILE_APPEND_DATA, FILE_SHARE_READ, 0,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        sayWarn(L"could not open the log file %s (%s) - continuing without a log",
                fullPath, winErrText(err));
        return;
    }
    g_log = h;
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t header[512];
    _snwprintf(header, 500,
               L"\n===== %04u-%02u-%02u %02u:%02u:%02u  BCD3000 installer =====\n",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    header[500] = 0;
    writeUtf8(g_log, header);
}

void logClose()
{
    if (g_log != INVALID_HANDLE_VALUE) {
        CloseHandle(g_log);
        g_log = INVALID_HANDLE_VALUE;
    }
}

bool logIsOpen()
{
    return g_log != INVALID_HANDLE_VALUE;
}

const wchar_t* winErrText(DWORD err)
{
    static wchar_t buf[512];
    wchar_t* msg = 0;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                             FORMAT_MESSAGE_IGNORE_INSERTS,
                             0, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                             (LPWSTR)&msg, 0, 0);
    if (n && msg) {
        while (n > 0 && (msg[n - 1] == L'\r' || msg[n - 1] == L'\n' || msg[n - 1] == L'.')) {
            msg[n - 1] = 0;
            n--;
        }
        _snwprintf(buf, 500, L"error %lu: %s", err, msg);
    } else {
        _snwprintf(buf, 500, L"error %lu", err);
    }
    buf[500] = 0;
    if (msg)
        LocalFree(msg);
    return buf;
}

void pauseIfWeOwnTheConsole()
{
    if (!g_consoleEcho)
        return;   // window mode: there is no console left to keep open
    DWORD pids[4] = { 0, 0, 0, 0 };
    DWORD n = GetConsoleProcessList(pids, 4);
    if (n != 1)
        return;   // started from an existing shell: the output stays on screen
    say(L"");
    say(L"Press Enter to close this window.");
    wchar_t line[16];
    fgetws(line, 16, stdin);
}

bool askYesNo(const wchar_t* question)
{
    // A window is asking instead of a console. The answer is logged in the same
    // words as the console answer below, on purpose: the log is what a support
    // request carries, and it must not matter which mode produced it.
    if (g_askHook) {
        bool yes = g_askHook(question);
        sayInfo(L"question: %s -> %s", question, yes ? L"yes" : L"no");
        return yes;
    }

    wchar_t prompt[1024];
    _snwprintf(prompt, 1000, L"%s [y/N] ", question);
    prompt[1000] = 0;
    toConsole(prompt);

    wchar_t line[64];
    if (!fgetws(line, 64, stdin)) {
        // End of input is not consent. A redirected or closed stdin must never
        // be read as a yes.
        sayInfo(L"question: %s -> no answer on standard input, taken as no", question);
        return false;
    }
    bool yes = (line[0] == L'y' || line[0] == L'Y');
    sayInfo(L"question: %s -> %s", question, yes ? L"yes" : L"no");
    return yes;
}

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

// The token of the account that owns the interactive desktop, taken from
// explorer.exe in this session. Zero when there is no explorer to ask or when
// opening its token was refused.
//
// Opened once and kept for the life of the process on purpose: every per user
// path has to resolve against the same account, and an answer that changed
// halfway through a run would put half of the install in one profile and half in
// another. The handle is not closed - the process is short lived and single
// threaded, and closing it early is how a later path lookup silently changes its
// answer.
static HANDLE g_interactiveToken      = 0;
static bool   g_interactiveTokenTried = false;
// WHICH of the two opens below succeeded. The weaker one answers "who owns the
// desktop" and nothing else; only the stronger one can be duplicated into the
// primary token CreateProcessWithTokenW takes. Recorded here rather than worked out
// again later, because "the token exists" and "the token can launch" are two
// different facts and collapsing them is how a page offers what it cannot do.
static bool   g_interactiveTokenFull  = false;

static HANDLE interactiveUserToken()
{
    if (g_interactiveTokenTried)
        return g_interactiveToken;
    g_interactiveTokenTried = true;

    DWORD mySession = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &mySession))
        return 0;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"explorer.exe") != 0)
                continue;
            DWORD session = 0xFFFFFFFF;
            if (!ProcessIdToSessionId(pe.th32ProcessID, &session) || session != mySession)
                continue;
            HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                   pe.th32ProcessID);
            if (!p)
                continue;
            // TOKEN_IMPERSONATE and TOKEN_DUPLICATE are what SHGetFolderPathW
            // documents it needs to resolve a folder for another user. QUERY
            // alone is enough to read the account, so it is tried as a fallback:
            // a token that only answers "who" is still better than none.
            HANDLE t = 0;
            bool   full = true;
            if (!OpenProcessToken(p, TOKEN_QUERY | TOKEN_IMPERSONATE | TOKEN_DUPLICATE, &t)) {
                full = false;
                OpenProcessToken(p, TOKEN_QUERY, &t);
            }
            CloseHandle(p);
            if (t) {
                g_interactiveToken     = t;
                g_interactiveTokenFull = full;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return g_interactiveToken;
}

// perUser selects whose profile the folder belongs to. See the block comment in
// common.h: the machine folders must not be resolved from a user token, and the
// per user folders must not be resolved from an administrator's token that
// happens to be running the installer.
static bool knownFolder(int csidl, wchar_t* out, DWORD count, bool perUser)
{
    if (count < MAX_PATH)
        return false;
    wchar_t buf[MAX_PATH];
    buf[0] = 0;

    HANDLE token = perUser ? interactiveUserToken() : 0;
    HRESULT hr = SHGetFolderPathW(0, csidl, token, SHGFP_TYPE_CURRENT, buf);
    if (FAILED(hr) && token) {
        // The desktop owner's profile could not be resolved - it may not be
        // loaded, or the token may only answer TOKEN_QUERY. Falling back to our
        // own profile keeps the program working; detectInteractiveAccount() is
        // what tells the user which account that turned out to be.
        buf[0] = 0;
        hr = SHGetFolderPathW(0, csidl, 0, SHGFP_TYPE_CURRENT, buf);
    }
    if (FAILED(hr) || buf[0] == 0)
        return false;

    wcsncpy(out, buf, count - 1);
    out[count - 1] = 0;
    return true;
}

bool getProgramFilesDir(wchar_t* out, DWORD count)
{
    return knownFolder(CSIDL_PROGRAM_FILES, out, count, false);
}
bool getLocalAppDataDir(wchar_t* out, DWORD count)
{
    return knownFolder(CSIDL_LOCAL_APPDATA, out, count, true);
}
bool getStartupDir(wchar_t* out, DWORD count)
{
    return knownFolder(CSIDL_STARTUP, out, count, true);
}

bool joinPath(wchar_t* out, DWORD count, const wchar_t* left, const wchar_t* right)
{
    if (!out || count == 0 || !left || !right)
        return false;
    size_t need = wcslen(left) + 1 + wcslen(right) + 1;
    if (need > count)
        return false;    // refuse rather than truncate into a different path
    _snwprintf(out, count - 1, L"%s\\%s", left, right);
    out[count - 1] = 0;
    return true;
}

bool installDirPath(wchar_t* out, DWORD count)
{
    wchar_t pf[kPathMax];
    if (!getProgramFilesDir(pf, kPathMax))
        return false;
    return joinPath(out, count, pf, kInstallDirName);
}

bool asioDllPath(wchar_t* out, DWORD count)
{
    wchar_t dir[kPathMax];
    if (!installDirPath(dir, kPathMax))
        return false;
    return joinPath(out, count, dir, kAsioDllFileName);
}

bool uninstallExePath(wchar_t* out, DWORD count)
{
    wchar_t dir[kPathMax];
    if (!installDirPath(dir, kPathMax))
        return false;
    return joinPath(out, count, dir, kUninstallExeName);
}

bool manifestPath(wchar_t* out, DWORD count)
{
    wchar_t dir[kPathMax];
    if (!installDirPath(dir, kPathMax))
        return false;
    return joinPath(out, count, dir, kManifestFileName);
}

bool bridgeDirPath(wchar_t* out, DWORD count)
{
    wchar_t lad[kPathMax];
    if (!getLocalAppDataDir(lad, kPathMax))
        return false;
    return joinPath(out, count, lad, kBridgeDirName);
}

bool bridgeExePath(wchar_t* out, DWORD count)
{
    wchar_t dir[kPathMax];
    if (!bridgeDirPath(dir, kPathMax))
        return false;
    return joinPath(out, count, dir, kBridgeExeFileName);
}

bool shortcutPath(wchar_t* out, DWORD count)
{
    wchar_t dir[kPathMax];
    if (!getStartupDir(dir, kPathMax))
        return false;
    return joinPath(out, count, dir, kShortcutFileName);
}

bool logFilePath(wchar_t* out, DWORD count)
{
    wchar_t dir[kPathMax];
    if (!bridgeDirPath(dir, kPathMax))
        return false;
    return joinPath(out, count, dir, kLogFileName);
}

// ---------------------------------------------------------------------------
// Filesystem
// ---------------------------------------------------------------------------
bool fileExists(const wchar_t* path)
{
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool dirExists(const wchar_t* path)
{
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool ensureDir(const wchar_t* path, DWORD* winErr)
{
    if (winErr)
        *winErr = 0;
    if (dirExists(path))
        return true;
    if (CreateDirectoryW(path, 0))
        return true;
    DWORD err = GetLastError();
    if (err == ERROR_ALREADY_EXISTS)
        return dirExists(path);
    if (winErr)
        *winErr = err;
    return false;
}

bool fileHasContent(const wchar_t* path, const void* data, DWORD size)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER fileSize;
    bool same = false;
    if (GetFileSizeEx(h, &fileSize) && fileSize.QuadPart == (LONGLONG)size) {
        const DWORD kChunk = 64 * 1024;
        BYTE* buf = (BYTE*)HeapAlloc(GetProcessHeap(), 0, kChunk);
        if (buf) {
            same = true;
            DWORD done = 0;
            while (done < size) {
                DWORD want = (size - done < kChunk) ? (size - done) : kChunk;
                DWORD got  = 0;
                if (!ReadFile(h, buf, want, &got, 0) || got != want) {
                    same = false;
                    break;
                }
                if (memcmp(buf, (const BYTE*)data + done, want) != 0) {
                    same = false;
                    break;
                }
                done += got;
            }
            HeapFree(GetProcessHeap(), 0, buf);
        }
    }
    CloseHandle(h);
    return same;
}

bool writeFileDirect(const wchar_t* path, const void* data, DWORD size, DWORD* winErr)
{
    if (winErr)
        *winErr = 0;

    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) {
        if (winErr)
            *winErr = GetLastError();
        return false;
    }

    bool  wrote = true;
    DWORD done  = 0;
    while (done < size) {
        DWORD written = 0;
        if (!WriteFile(h, (const BYTE*)data + done, size - done, &written, 0) || written == 0) {
            if (winErr)
                *winErr = GetLastError();
            wrote = false;
            break;
        }
        done += written;
    }
    if (wrote && !FlushFileBuffers(h)) {
        if (winErr)
            *winErr = GetLastError();
        wrote = false;
    }
    CloseHandle(h);

    if (!wrote)
        DeleteFileW(path);
    return wrote;
}

bool writeFileAtomic(const wchar_t* path, const void* data, DWORD size, DWORD* winErr)
{
    if (winErr)
        *winErr = 0;

    // Refuse rather than truncate: a truncated temporary name is a different
    // file, and we would then rename the wrong thing over the target.
    if (wcslen(path) + 24 >= kPathMax) {
        if (winErr)
            *winErr = ERROR_FILENAME_EXCED_RANGE;
        return false;
    }
    wchar_t tmp[kPathMax];
    _snwprintf(tmp, kPathMax - 1, L"%s.new%lu", path, GetCurrentProcessId());
    tmp[kPathMax - 1] = 0;

    if (!writeFileDirect(tmp, data, size, winErr))
        return false;

    // MOVEFILE_REPLACE_EXISTING fails when the target is open by another
    // process, which is exactly what we want: better to stop and say so than to
    // half replace a driver a host application is using.
    if (!MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING)) {
        if (winErr)
            *winErr = GetLastError();
        DeleteFileW(tmp);
        return false;
    }
    return true;
}

bool loadPayload(int resourceId, const void** data, DWORD* size)
{
    if (data) *data = 0;
    if (size) *size = 0;

    // RT_RCDATA expands to the narrow form unless UNICODE is defined, and this
    // build does not define it. The wide value is spelled out so the call cannot
    // silently pick the wrong resource type.
    const wchar_t* const kRcDataW = MAKEINTRESOURCEW(10);
    HRSRC found = FindResourceW(0, MAKEINTRESOURCEW(resourceId), kRcDataW);
    if (!found)
        return false;
    DWORD bytes = SizeofResource(0, found);
    HGLOBAL loaded = LoadResource(0, found);
    if (!loaded || bytes == 0)
        return false;
    const void* p = LockResource(loaded);
    if (!p)
        return false;
    if (data) *data = p;
    if (size) *size = bytes;
    return true;
}

bool checkDriverFile(const wchar_t* path, const wchar_t** whyNot, DWORD* winErr)
{
    if (whyNot) *whyNot = L"unknown";
    if (winErr) *winErr = 0;

    // Aligned by declaring it as 64 bit words: the PE header is read in place
    // through a struct pointer at an offset the file itself chooses.
    DWORD64 words[512];
    BYTE*   head = (BYTE*)words;
    ZeroMemory(words, sizeof(words));

    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) {
        if (winErr) *winErr = GetLastError();
        if (whyNot) *whyNot = L"the file could not be opened for reading";
        return false;
    }
    DWORD got  = 0;
    bool  read = ReadFile(h, head, (DWORD)sizeof(words), &got, 0) ? true : false;
    if (!read && winErr)
        *winErr = GetLastError();
    CloseHandle(h);

    if (!read || got < sizeof(IMAGE_DOS_HEADER)) {
        if (whyNot) *whyNot = L"the file is too small to be a DLL";
        return false;
    }

    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)head;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        if (whyNot) *whyNot = L"it is not a Windows executable at all (no MZ header)";
        return false;
    }
    if (dos->e_lfanew <= 0 ||
        (DWORD)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS64) > got) {
        if (whyNot) *whyNot = L"the PE header is not where the file says it is - the "
                              L"file is truncated or corrupt";
        return false;
    }
    const IMAGE_NT_HEADERS64* nt = (const IMAGE_NT_HEADERS64*)(head + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        if (whyNot) *whyNot = L"it has no PE header - the file is corrupt";
        return false;
    }
    // The one mistake that cannot be recovered from by trying again: a 32 bit or
    // ARM driver would be written over a working x64 one and only then refuse to
    // load into the host application.
    if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        if (whyNot) *whyNot = L"it is built for the wrong processor - this driver is "
                              L"64 bit x64 only";
        return false;
    }
    if ((nt->FileHeader.Characteristics & IMAGE_FILE_DLL) == 0) {
        if (whyNot) *whyNot = L"it is an executable, not a DLL";
        return false;
    }

    // Loading it is the only check that covers what a header cannot: truncation
    // further in, a missing dependency, a bad relocation. LOAD_WITH_ALTERED_
    // SEARCH_PATH so that anything it depends on is looked for next to it, which
    // is also how the registration call loads it a moment later.
    HMODULE mod = LoadLibraryExW(path, 0, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!mod) {
        if (winErr) *winErr = GetLastError();
        if (whyNot) *whyNot = L"it is a 64 bit DLL but Windows refused to load it";
        return false;
    }
    bool hasEntry = GetProcAddress(mod, "DllRegisterServer") != 0;
    FreeLibrary(mod);
    if (!hasEntry) {
        if (whyNot) *whyNot = L"it loads but exports no DllRegisterServer, so it is "
                              L"not this driver";
        return false;
    }

    if (whyNot) *whyNot = L"";
    return true;
}

bool writeTextFileUtf8(const wchar_t* path, const wchar_t* text, DWORD* winErr)
{
    if (winErr)
        *winErr = 0;
    int need = WideCharToMultiByte(CP_UTF8, 0, text, -1, 0, 0, 0, 0);
    if (need <= 1) {
        if (winErr)
            *winErr = ERROR_INVALID_DATA;
        return false;
    }
    char* buf = (char*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)need);
    if (!buf) {
        if (winErr)
            *winErr = ERROR_OUTOFMEMORY;
        return false;
    }
    bool ok = false;
    if (WideCharToMultiByte(CP_UTF8, 0, text, -1, buf, need, 0, 0) > 0)
        ok = writeFileAtomic(path, buf, (DWORD)(need - 1), winErr);
    else if (winErr)
        *winErr = GetLastError();
    HeapFree(GetProcessHeap(), 0, buf);
    return ok;
}

// ---------------------------------------------------------------------------
// The install manifest
// ---------------------------------------------------------------------------
static const wchar_t* const kManifestPreviousKey = L"previous_asio_registration";

// The text the setup writes when there was nothing registered before it ran. It
// is a placeholder and not a path, so it is spelled here only, next to the two
// functions that have to agree about it.
static const wchar_t* const kManifestNoPrevious = L"(none)";

bool findManifestValueIn(const wchar_t* text, const wchar_t* key,
                         wchar_t* out, DWORD count)
{
    if (!out || count == 0)
        return false;
    out[0] = 0;
    if (!text || !key || !*key)
        return false;

    wchar_t needle[128];
    _snwprintf(needle, 120, L"%s=", key);
    needle[120] = 0;
    size_t needleLen = wcslen(needle);

    // Only at the start of a line, so that a value containing the key name
    // cannot be mistaken for the key.
    const wchar_t* p = text;
    while (p && *p) {
        if (wcsncmp(p, needle, needleLen) == 0) {
            const wchar_t* v = p + needleLen;
            DWORD i = 0;
            while (v[i] && v[i] != L'\r' && v[i] != L'\n' && i < count - 1) {
                out[i] = v[i];
                i++;
            }
            out[i] = 0;
            return out[0] != 0;
        }
        const wchar_t* nl = wcschr(p, L'\n');
        p = nl ? nl + 1 : 0;
    }
    return false;
}

bool isRecordedRegistration(const wchar_t* value)
{
    return value && value[0] && _wcsicmp(value, kManifestNoPrevious) != 0;
}

bool readManifestValue(const wchar_t* key, wchar_t* out, DWORD count)
{
    if (!out || count == 0)
        return false;
    out[0] = 0;

    wchar_t path[kPathMax];
    if (!manifestPath(path, kPathMax) || !fileExists(path))
        return false;

    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    char  raw[8192];
    DWORD got  = 0;
    bool  read = ReadFile(h, raw, sizeof(raw) - 1, &got, 0) ? true : false;
    CloseHandle(h);
    if (!read || got == 0)
        return false;
    raw[got] = 0;

    wchar_t text[8192];
    if (MultiByteToWideChar(CP_UTF8, 0, raw, -1, text, 8192) == 0)
        return false;

    return findManifestValueIn(text, key, out, count);
}

bool readRecordedPreviousRegistration(wchar_t* out, DWORD count)
{
    if (!readManifestValue(kManifestPreviousKey, out, count))
        return false;
    if (!isRecordedRegistration(out)) {
        out[0] = 0;
        return false;
    }
    return true;
}

const wchar_t* manifestNoPreviousText()
{
    return kManifestNoPrevious;
}

bool stopBridge(int* terminated, DWORD* winErr)
{
    if (terminated) *terminated = 0;
    if (winErr)     *winErr     = 0;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        if (winErr)
            *winErr = GetLastError();
        return false;
    }

    // The processes are collected first and terminated afterwards. A one file
    // build is a bootloader plus a child, and killing the parent while still
    // walking the snapshot would leave the child behind holding the device.
    const int kMaxVictims = 16;
    HANDLE victims[kMaxVictims];
    int    count       = 0;
    bool   hitTheCap   = false;
    bool   couldNotOpen = false;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    // A snapshot always contains at least the system processes, so a first entry
    // that cannot be read is a real failure and not an empty machine. Left as a
    // success it produced the worst possible line in the log: "stopped 0
    // processes", followed by the caller carrying on as if the device were free.
    bool walked = Process32FirstW(snap, &pe) ? true : false;
    DWORD walkErr = walked ? 0 : GetLastError();
    if (walked) {
        do {
            if (_wcsicmp(pe.szExeFile, kBridgeExeFileName) != 0)
                continue;
            if (count >= kMaxVictims) {
                // Two processes is the normal count for a one file build. More than
                // sixteen means something else is going on, and reporting success
                // with survivors left behind would be a lie.
                hitTheCap = true;
                break;
            }
            HANDLE p = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID);
            if (p) {
                victims[count++] = p;
            } else {
                // The measured way to get here: elevating to a DIFFERENT
                // administrator account. The service belongs to the interactive
                // user and the default process DACL does not grant
                // PROCESS_TERMINATE to administrators without SeDebugPrivilege.
                // The process is still running and still holding the device.
                couldNotOpen = true;
                if (winErr && *winErr == 0)
                    *winErr = GetLastError();
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    bool allGone = walked && !hitTheCap && !couldNotOpen;
    if (!walked && winErr && *winErr == 0)
        *winErr = walkErr ? walkErr : ERROR_INVALID_FUNCTION;

    for (int i = 0; i < count; i++) {
        if (!TerminateProcess(victims[i], 1)) {
            DWORD err = GetLastError();
            // ERROR_ACCESS_DENIED on an already dead process is normal.
            if (WaitForSingleObject(victims[i], 0) != WAIT_OBJECT_0) {
                if (winErr && *winErr == 0)
                    *winErr = err;
                allGone = false;
            }
        }
        if (WaitForSingleObject(victims[i], 5000) != WAIT_OBJECT_0) {
            allGone = false;
            if (winErr && *winErr == 0)
                *winErr = WAIT_TIMEOUT;
        } else if (terminated) {
            (*terminated)++;
        }
        CloseHandle(victims[i]);
    }

    // The proof, and the only statement worth making. Everything above is what we
    // TRIED to do; a fresh snapshot is what actually happened. It also covers the
    // cases the loop cannot see: a child that was not in the first snapshot yet,
    // and an instance that started while we were working.
    if (allGone) {
        BridgeInfo left;
        detectBridge(&left);
        if (left.running) {
            allGone = false;
            if (winErr && *winErr == 0)
                *winErr = ERROR_BUSY;
        }
    }
    return allGone;
}

// ---------------------------------------------------------------------------
// Detection
// ---------------------------------------------------------------------------
static bool readRegString(HKEY root, const wchar_t* subKey, const wchar_t* valueName,
                          wchar_t* out, DWORD count)
{
    if (count == 0)
        return false;
    out[0] = 0;

    HKEY key;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;

    DWORD type  = 0;
    DWORD bytes = (count - 1) * sizeof(wchar_t);
    LONG  rc    = RegQueryValueExW(key, valueName, 0, &type, (LPBYTE)out, &bytes);
    RegCloseKey(key);

    if (rc != ERROR_SUCCESS)
        return false;
    // The type is checked instead of assumed: a REG_BINARY value read as a
    // string would be printed as whatever bytes happen to be there.
    if (type != REG_SZ && type != REG_EXPAND_SZ && type != REG_MULTI_SZ) {
        out[0] = 0;
        return false;
    }
    // Nothing in the registry guarantees a terminator: a value written with
    // RegSetValueEx and a length that excludes it - which is exactly what the
    // ASIO SDK's own createRegStringValue does - can come back without one.
    // Terminate by the length the call reported instead. The index is at most
    // count - 1 because the request was capped at (count - 1) * sizeof(wchar_t)
    // bytes.
    //
    // WHY THIS COMMENT USED TO NAME THE WRONG REASON. It said the values on this
    // machine happen to be terminated "because the ANSI wrapper inside the kernel
    // adds one", i.e. because RegSetValueExA converts to UTF-16 and terminates on
    // the way. That was measured and disproved by the probe written for step 8 of
    // task 12: over the *** Unicode *** path, where no conversion happens at all,
    // cbData = 58 comes back as 60 and cbData = 52 comes back as 52. What decides
    // is not ANSI versus Unicode - it is whether the reported size is EXACTLY the
    // length of the string, and in that case advapi32 appends the terminator on
    // both paths. The defensive termination below is still right, and for a better
    // reason: the guarantee is advapi32's undocumented behaviour either way, and a
    // value written by anything other than our own registrar (HKLM is editable by
    // an administrator) carries no promise at all. See item 2 at the top of
    // native/bcdasio/asioreg.h, which carries the same correction.
    out[bytes / sizeof(wchar_t)] = 0;
    out[count - 1] = 0;
    // For REG_MULTI_SZ the first string is the one that matters, and it is
    // already zero terminated in place.
    return out[0] != 0;
}

static bool regKeyExists(HKEY root, const wchar_t* subKey)
{
    HKEY key;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;
    RegCloseKey(key);
    return true;
}

// HKEY_CLASSES_ROOT is used here ON PURPOSE, and it is the only place in this
// file where the merged view is the right answer: the question this function
// answers is "which DLL would a host application load", and a host application
// sees exactly that merged view. Deleting keys is the opposite case - see
// deleteAsioRegistryKeys, which reads and deletes hive by hive.
void detectAsioRegistration(AsioRegInfo* out)
{
    ZeroMemory(out, sizeof(*out));

    wchar_t clsidKey[kPathMax];
    _snwprintf(clsidKey, kPathMax - 1, L"CLSID\\%s", kAsioClsid);
    clsidKey[kPathMax - 1] = 0;
    out->clsidKeyPresent = regKeyExists(HKEY_CLASSES_ROOT, clsidKey);

    wchar_t inprocKey[kPathMax];
    _snwprintf(inprocKey, kPathMax - 1, L"CLSID\\%s\\InprocServer32", kAsioClsid);
    inprocKey[kPathMax - 1] = 0;
    out->inprocPresent = readRegString(HKEY_CLASSES_ROOT, inprocKey, 0,
                                       out->inprocPath, kPathMax);

    wchar_t asioKey[kPathMax];
    _snwprintf(asioKey, kPathMax - 1, L"SOFTWARE\\ASIO\\%s", kAsioRegName);
    asioKey[kPathMax - 1] = 0;
    out->asioNameKeyPresent = regKeyExists(HKEY_LOCAL_MACHINE, asioKey);
    if (readRegString(HKEY_LOCAL_MACHINE, asioKey, L"CLSID",
                      out->asioNameClsid, 64)) {
        out->clsidMatches = (_wcsicmp(out->asioNameClsid, kAsioClsid) == 0);
    }

    // A per user entry wins over the machine one for that user, so a leftover
    // shadow can make a correct machine registration look broken.
    wchar_t shadow[kPathMax];
    _snwprintf(shadow, kPathMax - 1, L"Software\\Classes\\CLSID\\%s", kAsioClsid);
    shadow[kPathMax - 1] = 0;
    out->perUserShadowPresent = regKeyExists(HKEY_CURRENT_USER, shadow);
}

// Is the kernel driver registered with the Service Control Manager under that name?
//
// *** IT ASKS WHETHER THE SERVICE EXISTS AND NOTHING ELSE, WHICH IS THE WHOLE POINT
//     OF THE ACCESS MASK. *** SC_MANAGER_CONNECT and SERVICE_QUERY_STATUS are the
// cheapest rights that let OpenServiceW distinguish "there is such a service" from
// ERROR_SERVICE_DOES_NOT_EXIST, and both are granted to an ordinary user: measured
// unelevated, the whole call costs 1.0-1.2 ms and the negative - a name that cannot
// exist - comes back as a null handle with GetLastError()==1060.
//
// Nothing here starts, stops or configures anything, and nothing here loads a byte
// of anybody's library.
bool serviceIsRegistered(const wchar_t* serviceName)
{
    if (!serviceName || !serviceName[0])
        return false;
    SC_HANDLE scm = OpenSCManagerW(0, 0, SC_MANAGER_CONNECT);
    if (!scm)
        return false;
    SC_HANDLE svc = OpenServiceW(scm, serviceName, SERVICE_QUERY_STATUS);
    bool found = (svc != 0);
    if (svc)
        CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return found;
}

// ---------------------------------------------------------------------------
// A FILE'S VERSION RESOURCE, AS FOUR NUMBERS.
//
// *** THIS IS A READ OF A RESOURCE SECTION AND NOT A LOAD OF A MODULE. ***
// GetFileVersionInfoSizeW/GetFileVersionInfoW map the file's resources; they do
// not run DllMain, do not resolve imports and do not put the file in this
// process's module list. That distinction is the whole reason this program is
// allowed to ask about Microsoft's MIDI transport at all: it never gets loaded,
// never gets called, and no port is created to find anything out.
//
// *** AND IT READS VS_FIXEDFILEINFO AND NOT THE "FileVersion" STRING. *** The
// string is localised - on the owner's machine the file's descriptions come
// back in Portuguese - and is free-form, so parsing it would be parsing a
// translation. The fixed block is four binary numbers with no language on them.
// ---------------------------------------------------------------------------
bool fileVersionNumbers(const wchar_t* path, DWORD out[4], DWORD* winErr)
{
    if (winErr)
        *winErr = 0;
    if (out) {
        out[0] = out[1] = out[2] = out[3] = 0;
    }
    if (!path || !path[0] || !out) {
        if (winErr)
            *winErr = ERROR_INVALID_PARAMETER;
        return false;
    }

    DWORD handle = 0;
    DWORD size   = GetFileVersionInfoSizeW(path, &handle);
    if (size == 0) {
        if (winErr)
            *winErr = GetLastError();
        return false;
    }

    // Heap rather than a fixed buffer: the size is whatever the file says, and a
    // stack array big enough for every file is a guess this does not have to make.
    void* buf = HeapAlloc(GetProcessHeap(), 0, size);
    if (!buf) {
        if (winErr)
            *winErr = ERROR_NOT_ENOUGH_MEMORY;
        return false;
    }

    bool ok = false;
    if (GetFileVersionInfoW(path, 0, size, buf)) {
        VS_FIXEDFILEINFO* fixed = 0;
        UINT              len   = 0;
        if (VerQueryValueW(buf, L"\\", (LPVOID*)&fixed, &len) &&
            fixed && len >= sizeof(VS_FIXEDFILEINFO) &&
            fixed->dwSignature == 0xFEEF04BD) {
            out[0] = HIWORD(fixed->dwFileVersionMS);
            out[1] = LOWORD(fixed->dwFileVersionMS);
            out[2] = HIWORD(fixed->dwFileVersionLS);
            out[3] = LOWORD(fixed->dwFileVersionLS);
            ok = true;
        } else if (winErr) {
            *winErr = ERROR_RESOURCE_TYPE_NOT_FOUND;
        }
    } else if (winErr) {
        *winErr = GetLastError();
    }
    HeapFree(GetProcessHeap(), 0, buf);
    return ok;
}

// ---------------------------------------------------------------------------
// WHAT WINDOWS MIDI SERVICES LOOKS LIKE ON THIS MACHINE.
//
// Three reads, all of them cheap and none of them a port. See the block over
// WinMidiInfo in common.h for why it is exactly these three and why creating a
// port to be sure is forbidden rather than merely avoided.
//
// *** WHY THE BUILD COMES OFF midisrv.exe AND NOT OFF THE TRANSPORT DLL. ***
// The brief for this work named the transport DLL's file version as the reading,
// and on the owner's machine on 2026-08-02 that version is 1.0.15.0 - the
// Windows MIDI Services COMPONENT version, which cannot be compared with the
// 26100.8875 the issue's own title names. midisrv.exe, in the same directory
// and from the same update, reports 10.0.26100.8972: major.minor.BUILD.REVISION,
// which is the shape the known-bad list is written in. So BOTH are read - the
// transport DLL because it is the file the defect lives in and the file whose
// presence proves the stack is there at all, and the service binary because it
// is the one that carries the build the defect is indexed by.
//
// *** AND NOT THE REGISTRY'S UBR EITHER. *** HKLM's CurrentBuild/UBR said
// 26200.8973 on the same machine and the same minute that midisrv.exe said
// 26100.8972. Those are different numbers about different things: the registry
// describes the image's branding, the binary describes the binary. The defect
// is in a binary, so the binary is asked.
// ---------------------------------------------------------------------------
void detectWindowsMidi(WinMidiInfo* out)
{
    ZeroMemory(out, sizeof(*out));

    out->serviceRegistered = serviceIsRegistered(kMidiServiceName);

    wchar_t sysDir[MAX_PATH];
    sysDir[0] = 0;
    if (GetSystemDirectoryW(sysDir, MAX_PATH) == 0 || sysDir[0] == 0) {
        out->lastError = GetLastError();
        return;
    }

    wchar_t transport[kPathMax];
    if (joinPath(transport, kPathMax, sysDir, kMidiTransportDllName)) {
        out->transportPresent = fileExists(transport);
        if (out->transportPresent) {
            DWORD err = 0;
            out->transportVersionRead =
                fileVersionNumbers(transport, out->transportVersion, &err);
            if (!out->transportVersionRead && out->lastError == 0)
                out->lastError = err;
        }
    }

    wchar_t svcExe[kPathMax];
    if (joinPath(svcExe, kPathMax, sysDir, kMidiServiceExeName)) {
        DWORD err = 0;
        out->serviceVersionRead =
            fileVersionNumbers(svcExe, out->serviceVersion, &err);
        if (!out->serviceVersionRead && out->lastError == 0)
            out->lastError = err;
    }
}

// ---------------------------------------------------------------------------
// THE THREE STATES, AND WHY THE THIRD ONE IS "ANYTHING ELSE" RATHER THAN A LIST.
//
// kWinMidiReady and kWinMidiKnownBad both require the WHOLE reading: the service
// registered, the transport file there, and a build number that was actually
// read. Every other combination - including "everything is there but the version
// resource would not answer" - is kWinMidiUnread, which prints the numbers and
// states no cause. That is deliberate: a partial reading dressed up as a verdict
// is the defect class this program keeps removing from its own text.
// ---------------------------------------------------------------------------
WinMidiState classifyWindowsMidi(const WinMidiInfo* w)
{
    if (!w || !w->serviceRegistered || !w->transportPresent ||
        !w->transportVersionRead || !w->serviceVersionRead)
        return kWinMidiUnread;
    return midiBuildIsKnownBad(w->serviceVersion[2], w->serviceVersion[3])
               ? kWinMidiKnownBad
               : kWinMidiReady;
}

// Does ONE function key of the device carry a WinUSB interface guid on any of the
// ports it has been plugged into?
//
// *** ONE FUNCTION FOR BOTH QUESTIONS, AND THAT IS A CORRECTNESS PROPERTY. *** The
// check on MI_00 and the search over its siblings have to ask exactly the same
// question. A sibling search that accepted less than the binding check accepts would
// tell somebody "you bound interface 1" on the strength of a test interface 0 would
// have failed - and the action that sentence produces is another run of Zadig, which
// is the one operation in this whole process that can leave the hardware unusable.
//
// guidOut may be null, which is what the sibling search passes: it wants to know
// WHETHER, not WHICH.
static bool functionHasInterfaceGuid(const wchar_t* functionKey, wchar_t* guidOut,
                                     DWORD guidCap)
{
    HKEY root;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, functionKey, 0, KEY_READ, &root)
            != ERROR_SUCCESS)
        return false;

    // The device leaves one subkey per USB port it has been plugged into, and
    // only the ones bound to WinUSB carry the interface guid. Any one of them is
    // enough: the guid is a property of the binding, not of the port.
    bool found = false;
    for (DWORD i = 0; !found; i++) {
        wchar_t sub[256];
        DWORD   subLen = 256;
        if (RegEnumKeyExW(root, i, sub, &subLen, 0, 0, 0, 0) != ERROR_SUCCESS)
            break;

        // *** %.512s AND %.128s RATHER THAN %s, AND THE REASON IS THE RULE THIS
        // PROJECT WROTE AFTER kStageBadGuid. *** `sub` is a key NAME read out of
        // HKLM, which is editable by an administrator, so it is input from OUTSIDE
        // this program - and input from outside is bounded IN THE FORMAT STRING, at
        // the point of use, not by whoever happened to call this. Neither piece can
        // reach kPathMax on its own and neither can push the other out.
        wchar_t paramKey[kPathMax];
        _snwprintf(paramKey, kPathMax - 1, L"%.512s\\%.128s\\Device Parameters",
                   functionKey, sub);
        paramKey[kPathMax - 1] = 0;

        // Same two value names, in the same order, that native/bcdasio/usbdev.cpp
        // looks for.
        static const wchar_t* const names[2] = { L"DeviceInterfaceGUIDs",
                                                 L"DeviceInterfaceGUID" };
        for (int n = 0; n < 2 && !found; n++) {
            // 512 characters, to match the char val[512] that
            // native/bcdasio/usbdev.cpp reads the same value into. A buffer
            // smaller than the driver's turns a value in between the two sizes
            // into "not bound", and that answer sends the user back to Zadig -
            // the one operation in this whole process that can leave the
            // hardware unusable. The two sizes stay equal on purpose.
            wchar_t value[512];
            value[0] = 0;
            if (readRegString(HKEY_LOCAL_MACHINE, paramKey, names[n], value, 512)) {
                if (guidOut && guidCap > 0) {
                    wcsncpy(guidOut, value, guidCap - 1);
                    guidOut[guidCap - 1] = 0;
                }
                found = true;
            }
        }
    }
    RegCloseKey(root);
    return found;
}

static int hexDigit(wchar_t c)
{
    if (c >= L'0' && c <= L'9') return (int)(c - L'0');
    if (c >= L'a' && c <= L'f') return 10 + (int)(c - L'a');
    if (c >= L'A' && c <= L'F') return 10 + (int)(c - L'A');
    return -1;
}

// WHICH OTHER FUNCTION OF THIS DEVICE WAS BOUND, or -1 when none was.
//
// The composite device publishes one Enum\USB key per function - MI_00, MI_01 - and
// Zadig's list shows one line per function. Picking the wrong line binds the wrong
// key, and the symptom is identical to never having run Zadig at all: MI_00 has no
// guid. This is what separates them.
//
// READ ONLY, like everything gatherMachineState() reaches. It opens keys for reading,
// enumerates names and reads values; nothing here writes, and nothing here touches a
// device.
//
// THE FUNCTION NUMBER NEVER LEAVES THIS FUNCTION AS TEXT. What the page eventually
// prints is the int returned here, so no byte read out of the registry reaches a
// sentence - which is a stronger position than sanitising one, and it is available
// because the only thing wanted from the key name is a number.
//
// THE LOWEST MATCH WINS, so the answer does not depend on the order the registry
// happens to enumerate in. Two bound siblings is not a state anybody has produced,
// but "whichever came first" would be a page that says different things on two runs
// of the same machine.
static int siblingFunctionWithGuid()
{
    HKEY parent;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kUsbEnumParentKey, 0, KEY_READ, &parent)
            != ERROR_SUCCESS)
        return -1;

    const size_t prefixLen = wcslen(kUsbFunctionPrefix);
    int best = -1;
    for (DWORD i = 0; ; i++) {
        wchar_t name[256];
        DWORD   nameLen = 256;
        if (RegEnumKeyExW(parent, i, name, &nameLen, 0, 0, 0, 0) != ERROR_SUCCESS)
            break;
        if (_wcsnicmp(name, kUsbFunctionPrefix, prefixLen) != 0)
            continue;

        // EXACTLY two hex digits and then the end of the name. Both halves matter:
        // a prefix test alone would accept VID_1397&PID_00BF&MI_00_SOMETHING, and a
        // key somebody can create is not a key to draw a conclusion from.
        const wchar_t* d = name + prefixLen;
        int fn = 0, digits = 0;
        while (digits < 2 && hexDigit(d[digits]) >= 0) {
            fn = fn * 16 + hexDigit(d[digits]);
            digits++;
        }
        if (digits != 2 || d[2] != 0)
            continue;
        // Function 0 is the one the caller has already answered for. Finding a guid
        // on it here would contradict guidPresent being false.
        if (fn == 0)
            continue;
        if (best >= 0 && fn >= best)
            continue;

        wchar_t key[kPathMax];
        _snwprintf(key, kPathMax - 1, L"%.512s\\%.128s", kUsbEnumParentKey, name);
        key[kPathMax - 1] = 0;
        if (functionHasInterfaceGuid(key, 0, 0))
            best = fn;
    }
    RegCloseKey(parent);
    return best;
}

void detectWinUsbBinding(WinUsbInfo* out)
{
    ZeroMemory(out, sizeof(*out));
    // NOT the zero ZeroMemory just wrote, and the difference is the whole point of
    // the field: see the block on it in common.h.
    out->guidOnOtherFunction = -1;

    HKEY root;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kUsbEnumKey, 0, KEY_READ, &root) != ERROR_SUCCESS) {
        out->lastError = GetLastError();
        return;
    }
    RegCloseKey(root);
    out->enumKeyPresent = true;

    out->guidPresent = functionHasInterfaceGuid(kUsbEnumKey, out->guid, 64);

    if (!out->guidPresent) {
        out->guidOnOtherFunction = siblingFunctionWithGuid();
        return;
    }

    // Enumeration only. It asks the system which interfaces are present and
    // never opens the device, so it cannot take the device away from whoever is
    // using it right now.
    GUID guid;
    if (CLSIDFromString(out->guid, &guid) != NOERROR) {
        out->lastError = ERROR_INVALID_DATA;
        return;
    }
    HDEVINFO set = SetupDiGetClassDevsW(&guid, 0, 0,
                                        DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) {
        out->lastError = GetLastError();
        return;
    }
    SP_DEVICE_INTERFACE_DATA did;
    did.cbSize = sizeof(did);
    out->interfacePresentNow = SetupDiEnumDeviceInterfaces(set, 0, &guid, 0, &did) ? true : false;
    SetupDiDestroyDeviceInfoList(set);
}

void detectBridge(BridgeInfo* out)
{
    ZeroMemory(out, sizeof(*out));

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, kBridgeExeFileName) != 0)
                continue;
            out->running = true;
            out->instanceCount++;
            if (out->firstPid == 0) {
                out->firstPid = pe.th32ProcessID;
                HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                       pe.th32ProcessID);
                if (p) {
                    DWORD n = kPathMax;
                    if (!QueryFullProcessImageNameW(p, 0, out->imagePath, &n))
                        out->imagePath[0] = 0;
                    CloseHandle(p);
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

bool isElevated()
{
    HANDLE token = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elevation;
    DWORD size = sizeof(elevation);
    bool  yes  = false;
    if (GetTokenInformation(token, TokenElevation, &elevation, size, &size))
        yes = elevation.TokenIsElevated != 0;
    CloseHandle(token);
    return yes;
}

// Copies the user SID out of a token into caller memory.
static PSID dupTokenUser(HANDLE token)
{
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, 0, 0, &size);
    if (size == 0)
        return 0;
    BYTE* buf = (BYTE*)HeapAlloc(GetProcessHeap(), 0, size);
    if (!buf)
        return 0;
    if (!GetTokenInformation(token, TokenUser, buf, size, &size)) {
        HeapFree(GetProcessHeap(), 0, buf);
        return 0;
    }
    PSID src = ((TOKEN_USER*)buf)->User.Sid;
    DWORD sidLen = GetLengthSid(src);
    PSID copy = (PSID)HeapAlloc(GetProcessHeap(), 0, sidLen);
    if (copy && !CopySid(sidLen, copy, src)) {
        HeapFree(GetProcessHeap(), 0, copy);
        copy = 0;
    }
    HeapFree(GetProcessHeap(), 0, buf);
    return copy;
}

static void sidToAccount(PSID sid, wchar_t* out, DWORD count)
{
    out[0] = 0;
    wchar_t name[256], domain[256];
    DWORD nameLen = 256, domainLen = 256;
    SID_NAME_USE use;
    if (LookupAccountSidW(0, sid, name, &nameLen, domain, &domainLen, &use)) {
        _snwprintf(out, count - 1, L"%s\\%s", domain, name);
        out[count - 1] = 0;
        return;
    }
    wchar_t* text = 0;
    if (ConvertSidToStringSidW(sid, &text) && text) {
        wcsncpy(out, text, count - 1);
        out[count - 1] = 0;
        LocalFree(text);
    } else {
        wcsncpy(out, L"(unknown account)", count - 1);
        out[count - 1] = 0;
    }
}

// Answers the question that a per user install has to answer before it writes
// anything: is the account this process runs as the same account that owns the
// desktop? An elevated process started with a different administrator account
// would otherwise install the control service for a user who never signs in.
//
// It reads the SAME token the per user paths are resolved from, so the report and
// the paths can never disagree. When there is no explorer to ask (a bare session,
// or the process is denied), the answer is "not checked" rather than a guess -
// and in that case the paths fell back to this process's own profile too.
//
// RESIDUAL RISK, NAMED AND NOT FIXED HERE: the per user files are written by an
// elevated process into a tree the user can write to, so a user who prepares a
// junction there before running the setup can aim an elevated write somewhere
// else. The complete fix is to impersonate the desktop owner for those writes.
// It is not done, deliberately: it would have to wrap the COM shortcut save as
// well, a missed RevertToSelf() would run the rest of the install - including the
// HKLM registry work - under the wrong identity, and none of it can be exercised
// on this machine. What is done instead is the cheap half of the same defence:
// the setup REFUSES the per user half outright when the accounts differ, so an
// elevated write into another account's profile never happens at all.
void detectInteractiveAccount(AccountInfo* out)
{
    ZeroMemory(out, sizeof(*out));

    HANDLE self = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &self))
        return;
    PSID mine = dupTokenUser(self);
    CloseHandle(self);
    if (!mine)
        return;
    sidToAccount(mine, out->tokenAccount, 256);

    HANDLE shellToken = interactiveUserToken();
    PSID   shell      = shellToken ? dupTokenUser(shellToken) : 0;

    if (shell) {
        out->checked = true;
        out->matched = EqualSid(mine, shell) ? true : false;
        sidToAccount(shell, out->shellAccount, 256);
        HeapFree(GetProcessHeap(), 0, shell);
    }
    HeapFree(GetProcessHeap(), 0, mine);
}

// Which Windows this is. See the block over OsInfo in common.h for why
// GetVersionEx is honest in this build and why the manifest is not touched.
//
// THE DEPRECATION IS DELIBERATE AND SUPPRESSED IN ONE PLACE ONLY. Windows marks
// GetVersionExW deprecated to push callers at the VersionHelpers macros, and those
// answer "is it at least X" - which cannot distinguish Windows 10 from Windows 11,
// because IsWindows10OrGreater() is true on both. This function needs the build
// number, so it asks for it, and the pragma is around this call and nothing else.
void detectWindowsVersion(OsInfo* out)
{
    ZeroMemory(out, sizeof(*out));

    OSVERSIONINFOEXW vi;
    ZeroMemory(&vi, sizeof(vi));
    vi.dwOSVersionInfoSize = sizeof(vi);
#pragma warning(push)
#pragma warning(disable : 4996)
    if (!GetVersionExW((OSVERSIONINFOW*)&vi))
        return;
#pragma warning(pop)

    out->read  = true;
    out->major = vi.dwMajorVersion;
    out->minor = vi.dwMinorVersion;
    out->build = vi.dwBuildNumber;
    out->isWindows10 = (vi.dwMajorVersion == 10 && vi.dwMinorVersion == 0 &&
                        vi.dwBuildNumber < kFirstWindows11Build);
}

// ---------------------------------------------------------------------------
// WINGET, AND LAUNCHING SOMEBODY ELSE'S INSTALLER WITHOUT SPEAKING FOR THEM
//
// Everything in this block is read only except launchUnelevated(), which starts a
// process and nothing else: it writes no file, touches no registry key and installs
// nothing itself. What it installs is decided entirely by the command line it is
// handed, and there is exactly one builder for that - see the comments in common.h,
// which carry the reasoning this block only implements.
// ---------------------------------------------------------------------------

bool interactiveTokenCanLaunch()
{
    return interactiveUserToken() != 0 && g_interactiveTokenFull;
}

// *** FIX ROUND 1: buildWingetInstallCommand() STOOD HERE AND IS DELETED, BECAUSE
//     NOTHING IN THIS PROGRAM OR IN THE REST OF THIS PLAN CALLS IT. ***
//
// It built "winget.exe install --id <id> --source winget --exact" and its whole
// point was the flags it refused to write: --silent, /quiet, /silent, -h,
// --disable-interactivity, --accept-package-agreements, --accept-source-agreements.
// The harness asserted that refusal on the string it returned rather than on the
// bytes of the built exe, for a reason worth keeping in the record even though the
// function is gone: an absence check over a binary goes green the moment a flag is
// ASSEMBLED instead of written - L"--" L"silent", a swprintf, a resource string -
// so it would have passed for exactly the refactor it existed to stop.
//
// *** WHY IT GOES RATHER THAN STAYS, STATED OUT LOUD BECAUSE THE SPEC REQUIRES THE
//     CHOICE TO BE MADE AND NOT DRIFTED INTO. *** Its sole production caller was
// the MIDI port screen's accelerated offer, removed with the third party detection
// it was built on. Its sibling serviceIsRegistered() was KEPT in the same round on
// a named ground - Task 6 detects Windows MIDI Services by asking the SCM about the
// midisrv service, so that primitive has a reader coming. This one has none: Task 6
// "stops telling the user to install anything", and Task 7 ships its payload
// embedded in this installer rather than through a package manager. There is no
// task left in this plan that would call it, so keeping it would be keeping dead
// code with a story attached - which is the pattern this round deleted
// kThirdPartyWaitMs for, and it cannot be right to delete one and keep the other.
//
// THE CONTRACT ITSELF DOES NOT GO. The standing instruction to any future offer -
// never suppress the author's own interface, always unelevated in the desktop
// owner's session - is in common.h over the winget block, where somebody building
// one will read it before writing a line. What is deleted is an implementation of
// it that nothing invokes, not the rule.

// How long winget gets to answer "--version". Generous enough for a cold start and
// short enough that a wedged App Installer cannot wedge this program: the whole
// point of asking is to decide which of two buttons to offer.
static const DWORD kWingetProbeMs = 8000;

void detectWinget(WingetInfo* out)
{
    ZeroMemory(out, sizeof(*out));
    out->state = kWingetMissing;

    SECURITY_ATTRIBUTES sa;
    ZeroMemory(&sa, sizeof(sa));
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE rd = 0, wr = 0;
    if (!CreatePipe(&rd, &wr, &sa, 4096)) {
        out->state     = kWingetBlocked;
        out->lastError = GetLastError();
        return;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput  = wr;
    si.hStdError   = wr;
    si.hStdInput   = GetStdHandle(STD_INPUT_HANDLE);

    // Not "where winget". An execution alias that exists on disk proves nothing
    // about whether it runs, and one that is missing from this process's PATH
    // proves nothing about the user's. Running it is the only honest question.
    wchar_t cmd[64];
    wcscpy(cmd, L"winget.exe --version");

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    BOOL  ok  = CreateProcessW(0, cmd, 0, 0, TRUE, CREATE_NO_WINDOW, 0, 0, &si, &pi);
    DWORD err = ok ? 0 : GetLastError();
    CloseHandle(wr);                       // ours, so the read end can see the end
    if (!ok) {
        CloseHandle(rd);
        out->lastError = err;
        out->state = (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
                     ? kWingetMissing : kWingetBlocked;
        return;
    }

    // The deadline is what makes the 4096 byte pipe safe. "--version" prints one
    // short line, so it cannot fill the pipe and block - but a build that somehow
    // printed more would stop rather than hang here, and be reported as blocked.
    DWORD waited = WaitForSingleObject(pi.hProcess, kWingetProbeMs);
    if (waited != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        CloseHandle(rd);
        out->state     = kWingetBlocked;
        out->lastError = WAIT_TIMEOUT;
        return;
    }

    char  raw[256];
    DWORD got   = 0;
    DWORD avail = 0;
    raw[0] = 0;
    if (PeekNamedPipe(rd, 0, 0, 0, &avail, 0) && avail > 0) {
        DWORD want = avail < sizeof(raw) - 1 ? avail : (DWORD)(sizeof(raw) - 1);
        if (!ReadFile(rd, raw, want, &got, 0))
            got = 0;
    }
    raw[got] = 0;
    CloseHandle(rd);

    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    // winget prints plain ASCII here ("v1.9.25180"), so the widening is a copy and
    // not a code page decision. Anything that is not a printable ASCII character is
    // dropped rather than being carried into a string this program prints.
    int n = 0;
    for (DWORD i = 0; i < got && n < 63; i++)
        if (raw[i] >= 32 && raw[i] < 127)
            out->version[n++] = (wchar_t)raw[i];
    out->version[n] = 0;
    while (n > 0 && out->version[n - 1] == L' ')
        out->version[--n] = 0;

    out->lastError = code;
    if (code != 0) {
        // It is there and it refused. DisableAppInstaller is the usual reason, and
        // it is a different answer from "not installed" because a different thing
        // fixes it.
        out->state = kWingetBlocked;
        return;
    }
    out->state = kWingetUsable;
    if (!out->version[0])
        wcscpy(out->version, L"(it answered, but printed no version)");
}

bool launchUnelevated(const wchar_t* cmdline, DWORD* err, HANDLE* procOut)
{
    if (err)
        *err = 0;
    if (procOut)
        *procOut = 0;
    if (!cmdline || !cmdline[0] || wcslen(cmdline) >= kPathMax) {
        if (err)
            *err = ERROR_INVALID_PARAMETER;
        return false;
    }

    // REFUSED WHEN THE ACCOUNTS DIFFER, and the reason is the one already written
    // over detectInteractiveAccount(): an elevated process running as one account
    // must not act inside another account's session on that account's behalf. The
    // installer already refuses the per user half in exactly this case. The caller
    // falls back to opening a page, which asks nobody's identity for anything.
    AccountInfo who;
    detectInteractiveAccount(&who);
    if (!who.checked || !who.matched) {
        if (err)
            *err = ERROR_ACCESS_DENIED;
        return false;
    }

    HANDLE token = interactiveUserToken();
    if (!token || !g_interactiveTokenFull) {
        if (err)
            *err = ERROR_NO_TOKEN;
        return false;
    }

    // CreateProcessWithTokenW takes a PRIMARY token, and the one held here is the
    // desktop owner's process token opened for query, impersonation and
    // duplication. MAXIMUM_ALLOWED rather than TOKEN_ALL_ACCESS: the second asks
    // for rights this process may not be granted on somebody else's token, and
    // being refused the duplicate is a worse outcome than holding fewer rights.
    HANDLE primary = 0;
    if (!DuplicateTokenEx(token, MAXIMUM_ALLOWED, 0, SecurityImpersonation,
                          TokenPrimary, &primary)) {
        if (err)
            *err = GetLastError();
        return false;
    }

    wchar_t mutableCmd[kPathMax];
    wcsncpy(mutableCmd, cmdline, kPathMax - 1);
    mutableCmd[kPathMax - 1] = 0;

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    // lpDesktop is deliberately left null, so the child inherits THIS process's
    // window station and desktop. In the product that is winsta0\default, which is
    // where the user is. In installer/verify it is the private desktop the harness
    // moved itself to before creating anything - so even an elevated harness run
    // cannot put a window on the owner's screen.
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    // LOGON_WITH_PROFILE so the child gets the desktop owner's registry hive. The
    // environment block is inherited rather than built with CreateEnvironmentBlock,
    // and that is safe for one measured reason and not by luck: this function has
    // already refused unless the desktop owner IS the account this process runs as,
    // so our environment is already that account's.
    BOOL  ok = CreateProcessWithTokenW(primary, LOGON_WITH_PROFILE, 0, mutableCmd,
                                       0, 0, 0, &si, &pi);
    DWORD e  = ok ? 0 : GetLastError();
    CloseHandle(primary);
    if (!ok) {
        // The expected failure when this is called from a process that is NOT
        // elevated: CreateProcessWithTokenW needs SE_IMPERSONATE_NAME and a
        // filtered token does not carry it. ERROR_PRIVILEGE_NOT_HELD, 1314.
        if (err)
            *err = e;
        return false;
    }
    CloseHandle(pi.hThread);
    if (procOut)
        *procOut = pi.hProcess;
    else
        CloseHandle(pi.hProcess);
    return true;
}

bool isSafeUrlForCommandLine(const wchar_t* url)
{
    if (!url)
        return false;
    if (_wcsnicmp(url, L"http://", 7) != 0 && _wcsnicmp(url, L"https://", 8) != 0)
        return false;
    size_t n = wcslen(url);
    if (n < 12 || n > 400)
        return false;
    for (size_t i = 0; i < n; i++) {
        wchar_t c = url[i];
        // Printable ASCII only, so nothing outside it can be reinterpreted by a
        // code page; and none of the characters a command interpreter treats as
        // punctuation. Every url this program opens is a literal in this file and
        // passes, which is the state a boundary check should be in.
        if (c < L'!' || c > L'~')
            return false;
        if (c == L'"' || c == L'\'' || c == L'^' || c == L'&' || c == L'|' ||
            c == L'<' || c == L'>' || c == L'%' || c == L'`')
            return false;
    }
    return true;
}

bool openPageInBrowser(const wchar_t* url, DWORD* err)
{
    if (err)
        *err = 0;
    if (!isSafeUrlForCommandLine(url)) {
        if (err)
            *err = ERROR_INVALID_PARAMETER;
        return false;
    }

    // The unelevated route first, and not for tidiness. A browser started from this
    // elevated process would inherit our token and run elevated - which is a real
    // downgrade for the user, not a cosmetic one, and it is the same inheritance
    // rule that makes launchUnelevated() necessary in the first place. explorer.exe
    // is the launcher because it hands the address to the shell, which opens it
    // with whatever the user's default browser is.
    //
    // WHAT THIS CANNOT CONFIRM, said rather than implied: that the page opened. It
    // confirms that a launcher started. Nothing below reports otherwise.
    if (interactiveTokenCanLaunch()) {
        wchar_t cmd[kPathMax];
        _snwprintf(cmd, kPathMax - 1, L"explorer.exe \"%s\"", url);
        cmd[kPathMax - 1] = 0;
        DWORD  e = 0;
        HANDLE p = 0;
        if (launchUnelevated(cmd, &e, &p)) {
            if (p)
                CloseHandle(p);
            return true;
        }
    }

    // No usable token, or the accounts differ. ShellExecuteW still gets the user to
    // the page; it may open elevated, which is worse than the route above and much
    // better than no page at all.
    HINSTANCE r = ShellExecuteW(0, L"open", url, 0, 0, SW_SHOWNORMAL);
    if ((INT_PTR)r > 32)
        return true;
    if (err)
        *err = (DWORD)(INT_PTR)r;   // ShellExecuteW's own code, not GetLastError
    return false;
}

// ---------------------------------------------------------------------------
// The whole machine state, and the report of it
// ---------------------------------------------------------------------------
void gatherMachineState(MachineState* out)
{
    ZeroMemory(out, sizeof(*out));

    out->pathsResolved =
        installDirPath(out->installDir, kPathMax) &&
        asioDllPath(out->dllTarget, kPathMax) &&
        bridgeExePath(out->bridgeTarget, kPathMax) &&
        shortcutPath(out->shortcutFile, kPathMax);

    out->elevated = isElevated();
    detectWindowsVersion(&out->os);
    detectInteractiveAccount(&out->account);
    detectAsioRegistration(&out->asio);
    detectWinUsbBinding(&out->usb);
    detectBridge(&out->bridge);
    // Read only, and no port is created: see the block over detectWindowsMidi().
    detectWindowsMidi(&out->winMidi);

    // The two facts page 2's offer is made of. Both are measurements, so both live
    // in the state with the rest of them - see the comment on the fields.
    //
    // THIS SPAWNS A PROCESS, and that is stated because "gatherMachineState is read
    // only" is a promise this folder keeps. It runs "winget --version" with a
    // deadline: it reads a version and changes nothing, which is the same kind of
    // act as reading a registry key, and it is the only way to tell an App
    // Installer that is missing from one that is blocked by policy.
    detectWinget(&out->winget);
    out->tokenCanLaunch = interactiveTokenCanLaunch();

    if (out->pathsResolved && fileExists(out->shortcutFile)) {
        out->shortcutPresent = true;
        readShortcutTarget(out->shortcutFile, out->shortcutPointsAt, kPathMax);
    }

    if (out->pathsResolved && out->asio.inprocPresent)
        out->registeredElsewhere = (_wcsicmp(out->asio.inprocPath, out->dllTarget) != 0);
}

void reportMachineState(const MachineState* s)
{
    say(L"--- where things go ---");
    if (!s->pathsResolved) {
        sayFail(L"could not resolve the Windows folders this product installs into");
    } else {
        sayInfo(L"driver          : %s", s->dllTarget);
        sayInfo(L"control service : %s", s->bridgeTarget);
        sayInfo(L"startup shortcut: %s", s->shortcutFile);
    }
    sayBlank();

    say(L"--- who is running this ---");
    // The version goes in the report, not only on the page, because the page's
    // Windows 10 row is absent on Windows 11 and an absent row is indistinguishable
    // from a check that was never made by anybody reading a support log.
    if (s->os.read)
        sayInfo(L"windows : %lu.%lu build %lu%s", (unsigned long)s->os.major,
                (unsigned long)s->os.minor, (unsigned long)s->os.build,
                s->os.isWindows10 ? L" (Windows 10)" : L"");
    else
        sayWarn(L"windows : the version could not be read");
    sayInfo(L"elevated: %s", s->elevated ? L"yes" : L"no");
    if (s->account.tokenAccount[0])
        sayInfo(L"account : %s", s->account.tokenAccount);
    if (s->account.checked && !s->account.matched) {
        sayWarn(L"the interactive desktop belongs to %s, not to %s",
                s->account.shellAccount, s->account.tokenAccount);
        sayWarn(L"the per user parts (control service and startup shortcut) would be "
                L"installed for %s and would never start for %s",
                s->account.tokenAccount, s->account.shellAccount);
    }
    sayBlank();

    say(L"--- 1. ASIO driver registration ---");
    if (!s->asio.clsidKeyPresent && !s->asio.asioNameKeyPresent) {
        sayInfo(L"not registered on this machine");
    } else {
        if (s->asio.inprocPresent)
            sayInfo(L"a host application would load: %s", s->asio.inprocPath);
        else
            sayWarn(L"the class id key exists but has no InprocServer32 path - the "
                    L"registration is incomplete");
        if (s->asio.asioNameKeyPresent && s->asio.clsidMatches)
            sayOk(L"listed under HKLM\\SOFTWARE\\ASIO as \"%s\"", kAsioRegName);
        else if (s->asio.asioNameKeyPresent)
            sayWarn(L"HKLM\\SOFTWARE\\ASIO\\%s points at class id %s, not at ours (%s)",
                    kAsioRegName, s->asio.asioNameClsid, kAsioClsid);
        else
            sayWarn(L"not listed under HKLM\\SOFTWARE\\ASIO - host applications will "
                    L"not offer the driver");
        if (s->asio.perUserShadowPresent)
            sayWarn(L"a per user registration exists at HKCU\\Software\\Classes\\CLSID\\%s "
                    L"and takes precedence over the machine one", kAsioClsid);
        if (s->registeredElsewhere)
            sayWarn(L"the registered copy is NOT the one in the install folder");
    }
    sayBlank();

    say(L"--- 2. control service (BCD3000Bridge.exe) ---");
    if (s->pathsResolved && fileExists(s->bridgeTarget))
        sayOk(L"installed");
    else
        sayInfo(L"not installed");
    if (s->bridge.running) {
        // A one file build shows up as a bootloader process plus its child, so
        // two processes is the normal count and not a duplicate installation.
        sayOk(L"running (%d process%s, first pid %lu)", s->bridge.instanceCount,
              s->bridge.instanceCount == 1 ? L"" : L"es", s->bridge.firstPid);
        if (s->bridge.imagePath[0])
            sayInfo(L"running from: %s", s->bridge.imagePath);
    } else {
        sayInfo(L"not running");
    }
    if (s->shortcutPresent) {
        sayOk(L"startup shortcut present");
        if (s->shortcutPointsAt[0]) {
            sayInfo(L"shortcut points at: %s", s->shortcutPointsAt);
            if (s->pathsResolved && _wcsicmp(s->shortcutPointsAt, s->bridgeTarget) != 0)
                sayWarn(L"the shortcut does not point at the install location");
        } else {
            sayWarn(L"the startup shortcut could not be read");
        }
    } else {
        sayInfo(L"no startup shortcut - the control service would not start at sign in");
    }
    sayBlank();

    // *** SECTION 3 USED TO REPORT A THIRD PARTY VIRTUAL MIDI DRIVER, AND THAT
    //     DETECTION IS GONE. *** MachineState no longer carries a reading of any
    // virtual MIDI backend - this task removed the third party detection without
    // adding its replacement, which is a later task's job. What is left here is
    // the generic winget and launch capability reading, kept because a future
    // screen may still need it.
    say(L"--- 3. winget and unelevated launch ---");
    switch (s->winget.state) {
    case kWingetUsable:
        sayInfo(L"winget  : %s", s->winget.version);
        break;
    case kWingetBlocked:
        sayWarn(L"winget  : present and it refused (%s) - probably the "
                L"DisableAppInstaller policy", winErrText(s->winget.lastError));
        break;
    case kWingetMissing:
    default:
        sayInfo(L"winget  : not available on this machine");
        break;
    }
    sayInfo(L"launching as the desktop owner, unelevated: %s",
            s->tokenCanLaunch ? L"available" : L"not available");
    sayBlank();

    say(L"--- 4. WinUSB binding on the device ---");
    if (!s->usb.enumKeyPresent) {
        sayFail(L"the BCD3000 has never been seen by this machine, or it is not bound "
                L"to WinUSB");
        sayInfo(L"registry key looked up: HKLM\\%s", kUsbEnumKey);
    } else if (!s->usb.guidPresent) {
        sayFail(L"the device is known but has no WinUSB interface - the binding is "
                L"missing on the MI_00 function");
        // The one thing that separates "Zadig has not been run" from "Zadig was run
        // on the wrong line". > 0 and not >= 0: see the block on the field in
        // common.h - 0 is what a zeroed struct carries, and it would contradict the
        // line above.
        if (s->usb.guidOnOtherFunction > 0)
            sayFail(L"but interface %d of the same device IS bound to WinUSB, so the "
                    L"wrong line of Zadig's list was picked. It has to be interface 0 "
                    L"(MI_00).", s->usb.guidOnOtherFunction);
    } else {
        sayOk(L"WinUSB interface recorded: %s", s->usb.guid);
        if (s->usb.interfacePresentNow)
            sayOk(L"and the interface is present right now (device connected)");
        else
            sayInfo(L"the interface is not enumerated right now - connect and power on "
                    L"the BCD3000 to use it");
    }
    if (!s->usb.enumKeyPresent || !s->usb.guidPresent) {
        sayInfo(L"this has to be done once, by hand, with Zadig: %s", kZadigDownloadPage);
        sayInfo(L"in Zadig choose the BCD3000 interface 0 (MI_00) and replace its driver "
                L"with WinUSB");
        sayInfo(L"this installer deliberately does NOT rebind the device: a wrong "
                L"binding leaves the hardware unusable");
    }
    sayBlank();
}

// ---------------------------------------------------------------------------
// Shortcut
// ---------------------------------------------------------------------------
bool readShortcutTarget(const wchar_t* lnkPath, wchar_t* target, DWORD count)
{
    if (count == 0)
        return false;
    target[0] = 0;
    if (!fileExists(lnkPath))
        return false;

    bool initialised = SUCCEEDED(CoInitializeEx(0, COINIT_APARTMENTTHREADED));
    bool got = false;
    IShellLinkW* link = 0;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, 0, CLSCTX_INPROC_SERVER,
                                   IID_IShellLinkW, (void**)&link)) && link) {
        IPersistFile* file = 0;
        if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, (void**)&file)) && file) {
            if (SUCCEEDED(file->Load(lnkPath, STGM_READ))) {
                wchar_t buf[MAX_PATH];
                buf[0] = 0;
                // No SLGP_ flags that would resolve or repair the link: we want
                // what is written in the file, not the shell's best guess.
                if (SUCCEEDED(link->GetPath(buf, MAX_PATH, 0, 0)) && buf[0]) {
                    wcsncpy(target, buf, count - 1);
                    target[count - 1] = 0;
                    got = true;
                }
            }
            file->Release();
        }
        link->Release();
    }
    if (initialised)
        CoUninitialize();
    return got;
}

bool createShortcut(const wchar_t* lnkPath, const wchar_t* target,
                    const wchar_t* workingDir, const wchar_t* description,
                    HRESULT* hrOut)
{
    HRESULT hr = E_FAIL;
    bool initialised = SUCCEEDED(CoInitializeEx(0, COINIT_APARTMENTTHREADED));

    IShellLinkW* link = 0;
    hr = CoCreateInstance(CLSID_ShellLink, 0, CLSCTX_INPROC_SERVER,
                          IID_IShellLinkW, (void**)&link);
    if (SUCCEEDED(hr) && link) {
        link->SetPath(target);
        if (workingDir)
            link->SetWorkingDirectory(workingDir);
        if (description)
            link->SetDescription(description);
        IPersistFile* file = 0;
        hr = link->QueryInterface(IID_IPersistFile, (void**)&file);
        if (SUCCEEDED(hr) && file) {
            hr = file->Save(lnkPath, TRUE);
            file->Release();
        }
        link->Release();
    }
    if (initialised)
        CoUninitialize();
    if (hrOut)
        *hrOut = hr;
    return SUCCEEDED(hr);
}

// ---------------------------------------------------------------------------
// COM self registration of the driver
// ---------------------------------------------------------------------------
typedef HRESULT (STDAPICALLTYPE *DllRegProc)(void);

static bool callSelfReg(const wchar_t* dllPath, const char* entryPoint,
                        HRESULT* hrOut, DWORD* winErr)
{
    if (hrOut)  *hrOut  = E_FAIL;
    if (winErr) *winErr = 0;

    // LOAD_WITH_ALTERED_SEARCH_PATH so that anything the driver depends on is
    // looked up next to the driver instead of next to this installer.
    HMODULE mod = LoadLibraryExW(dllPath, 0, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!mod) {
        if (winErr)
            *winErr = GetLastError();
        return false;
    }
    bool ok = false;
    DllRegProc entry = (DllRegProc)GetProcAddress(mod, entryPoint);
    if (!entry) {
        if (winErr)
            *winErr = GetLastError();
    } else {
        HRESULT hr = entry();
        if (hrOut)
            *hrOut = hr;
        ok = SUCCEEDED(hr);
    }
    FreeLibrary(mod);
    return ok;
}

bool callDllRegisterServer(const wchar_t* dllPath, HRESULT* hrOut, DWORD* winErr)
{
    return callSelfReg(dllPath, "DllRegisterServer", hrOut, winErr);
}

bool callDllUnregisterServer(const wchar_t* dllPath, HRESULT* hrOut, DWORD* winErr)
{
    return callSelfReg(dllPath, "DllUnregisterServer", hrOut, winErr);
}

// Removes one Classes\CLSID entry from ONE hive, and only when the driver path
// recorded IN THAT SAME HIVE is the copy we installed. Reading through
// HKEY_CLASSES_ROOT and deleting from a hive is what has to be avoided: the
// merged view answers with the per user shadow when there is one, so the check
// would pass on the shadow and the deletion would fall on the machine key.
static bool deleteClsidInHive(HKEY hive, const wchar_t* hiveName,
                              const wchar_t* clsidKey, const wchar_t* inprocKey,
                              const wchar_t* expectedDllPath, bool* removed)
{
    if (removed)
        *removed = false;
    if (!regKeyExists(hive, clsidKey))
        return true;

    wchar_t registered[kPathMax];
    registered[0] = 0;
    if (!readRegString(hive, inprocKey, 0, registered, kPathMax)) {
        sayWarn(L"%s\\%s exists but records no driver path - leaving it alone, because "
                L"there is nothing to check it against", hiveName, clsidKey);
        return true;
    }
    if (_wcsicmp(registered, expectedDllPath) != 0) {
        sayWarn(L"%s\\%s points at %s, which this uninstaller did not install - leaving "
                L"it alone", hiveName, clsidKey, registered);
        return true;
    }

    LONG rc = RegDeleteTreeW(hive, clsidKey);
    if (rc == ERROR_SUCCESS) {
        if (removed)
            *removed = true;
        return true;
    }
    if (rc == ERROR_FILE_NOT_FOUND)
        return true;
    sayFail(L"could not remove %s\\%s (%s)", hiveName, clsidKey, winErrText((DWORD)rc));
    return false;
}

bool deleteAsioRegistryKeys(const wchar_t* expectedDllPath, bool* removedClsid,
                            bool* removedAsioName)
{
    if (removedClsid)    *removedClsid    = false;
    if (removedAsioName) *removedAsioName = false;

    bool allGood = true;

    // The class id entry, hive by hive.
    wchar_t machineClsid[kPathMax], machineInproc[kPathMax];
    _snwprintf(machineClsid, kPathMax - 1, L"SOFTWARE\\Classes\\CLSID\\%s", kAsioClsid);
    machineClsid[kPathMax - 1] = 0;
    _snwprintf(machineInproc, kPathMax - 1, L"SOFTWARE\\Classes\\CLSID\\%s\\InprocServer32",
               kAsioClsid);
    machineInproc[kPathMax - 1] = 0;

    wchar_t userClsid[kPathMax], userInproc[kPathMax];
    _snwprintf(userClsid, kPathMax - 1, L"Software\\Classes\\CLSID\\%s", kAsioClsid);
    userClsid[kPathMax - 1] = 0;
    _snwprintf(userInproc, kPathMax - 1, L"Software\\Classes\\CLSID\\%s\\InprocServer32",
               kAsioClsid);
    userInproc[kPathMax - 1] = 0;

    bool removedMachine = false, removedUser = false;
    if (!deleteClsidInHive(HKEY_LOCAL_MACHINE, L"HKLM", machineClsid, machineInproc,
                           expectedDllPath, &removedMachine))
        allGood = false;

    // The per user shadow, treated explicitly instead of being left behind. It is
    // the entry that actually wins for this user, so removing the machine key and
    // leaving this one is how a "removed" driver keeps being loaded.
    //
    // LIMIT, on purpose: this is the hive of the account running the uninstaller.
    // When an administrator elevated into another user's session there is nothing
    // to load that other user's hive from, and mounting somebody else's hive to
    // delete keys in it is not something an uninstaller should be doing.
    if (regKeyExists(HKEY_CURRENT_USER, userClsid)) {
        sayInfo(L"a per user registration exists at HKCU\\%s and takes precedence over "
                L"the machine one", userClsid);
        if (!deleteClsidInHive(HKEY_CURRENT_USER, L"HKCU", userClsid, userInproc,
                               expectedDllPath, &removedUser))
            allGood = false;
        else if (removedUser)
            sayOk(L"the per user registration was ours and has been removed");
    }
    if (removedClsid)
        *removedClsid = removedMachine || removedUser;

    // The HKLM\SOFTWARE\ASIO entry, and only when it points at our class id.
    // The parent ASIO key is shared with every other ASIO driver on the machine
    // and is never touched.
    wchar_t asioKey[kPathMax];
    _snwprintf(asioKey, kPathMax - 1, L"SOFTWARE\\ASIO\\%s", kAsioRegName);
    asioKey[kPathMax - 1] = 0;
    wchar_t clsidValue[64];
    clsidValue[0] = 0;
    if (readRegString(HKEY_LOCAL_MACHINE, asioKey, L"CLSID", clsidValue, 64)) {
        if (_wcsicmp(clsidValue, kAsioClsid) == 0) {
            LONG rc = RegDeleteTreeW(HKEY_LOCAL_MACHINE, asioKey);
            if (rc == ERROR_SUCCESS) {
                if (removedAsioName)
                    *removedAsioName = true;
            } else if (rc != ERROR_FILE_NOT_FOUND) {
                sayFail(L"could not remove HKLM\\%s (%s)", asioKey, winErrText((DWORD)rc));
                allGood = false;
            }
        } else {
            sayWarn(L"HKLM\\%s points at class id %s, which is not ours - leaving it alone",
                    asioKey, clsidValue);
        }
    }

    return allGood;
}

}
