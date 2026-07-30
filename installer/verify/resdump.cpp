// Extracts the RCDATA payloads from a built executable, so that "what the
// installer is carrying" can be compared byte for byte with what is on disk.
//
// LOAD_LIBRARY_AS_DATAFILE: the module is mapped for reading only, and no entry
// point of it is ever called. This program cannot run the installer.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) {
        wprintf(L"usage: resdump <exe> <outdir>\n");
        return 2;
    }
    HMODULE m = LoadLibraryExW(argv[1], 0, LOAD_LIBRARY_AS_DATAFILE);
    if (!m) {
        wprintf(L"could not map %s (%lu)\n", argv[1], GetLastError());
        return 1;
    }
    const int      ids[4]   = { 101, 102, 103, 110 };
    const wchar_t* names[4] = { L"101-BcdAsio.dll", L"102-BCD3000Bridge.exe",
                                L"103-BCD3000Uninstall.exe", L"110-BCD3000.PNG" };
    int bad = 0;
    for (int i = 0; i < 4; i++) {
        // RT_RCDATA is 10. The macro itself expands to the ANSI
        // MAKEINTRESOURCE here, because this project does not define UNICODE.
        HRSRC h = FindResourceW(m, MAKEINTRESOURCEW(ids[i]), MAKEINTRESOURCEW(10));
        if (!h) {
            wprintf(L"%d: NOT PRESENT\n", ids[i]);
            bad++;
            continue;
        }
        HGLOBAL g = LoadResource(m, h);
        void*   p = g ? LockResource(g) : 0;
        DWORD   n = SizeofResource(m, h);
        if (!p || !n) {
            wprintf(L"%d: unreadable\n", ids[i]);
            bad++;
            continue;
        }
        wchar_t out[1024];
        _snwprintf(out, 1000, L"%s%s%s", argv[2], L"\\", names[i]);
        out[1000] = 0;
        HANDLE f = CreateFileW(out, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, 0);
        DWORD w = 0;
        if (f == INVALID_HANDLE_VALUE || !WriteFile(f, p, n, &w, 0) || w != n) {
            wprintf(L"%d: could not write %s\n", ids[i], out);
            bad++;
        } else {
            wprintf(L"%d: %lu bytes -> %s\n", ids[i], n, out);
        }
        if (f != INVALID_HANDLE_VALUE)
            CloseHandle(f);
    }
    FreeLibrary(m);
    return bad ? 1 : 0;
}
