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

// gui.cpp's message loop is renamed away; the name is redefined below.
#define runWizard runWizard_realMessageLoop
#include "gui.cpp"
#undef runWizard

namespace bcdgui { int runWizard(Wizard* wiz); }

#define main setupRealMain
#include "setup.cpp"
#undef main

// ---------------------------------------------------------------------------
// Counting. Every number this harness prints comes with its denominator.
// ---------------------------------------------------------------------------
static int g_checks = 0;
static int g_fails  = 0;

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

static int capCount(const wchar_t* needle)
{
    int n = 0;
    for (int i = 0; i < g_capN; i++)
        if (wcsstr(g_cap[i], needle))
            n++;
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
    s->tevm.state      = bcdsetup::kTeVmPresent;
    s->usb.enumKeyPresent     = true;
    s->usb.guidPresent        = true;
    s->usb.interfacePresentNow = true;
    wcscpy(s->usb.guid, L"{a5dcbf10-6530-11d2-901f-00c04fb951ed}");
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
    check(capHas(L"stopping it DESTROYS the virtual MIDI port"),
          L"A: the port sentence appears, and says destroyed");
    check(!capHas(L"was NOT touched by this install"),
          L"A: the \"not touched\" sentence is absent");
    check(!capHas(L"recreated"),
          L"A: the word \"recreated\" appears nowhere");

    // -------------------------------------------------------------------
    // Branch B: the driver file was replaced and the service was NOT touched.
    // This is the case the first real run of the installer hit, and the case in
    // which the old text was false.
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
    check(capHas(L"was NOT touched by this install"),
          L"B: it states the port was NOT touched");
    check(!capHas(L"DESTROYS the virtual MIDI port"),
          L"B: no claim that the port was destroyed");
    check(!capHas(L"recreated"),
          L"B: the word \"recreated\" appears nowhere");

    // -------------------------------------------------------------------
    // Branch C: only the registration moved. The old code printed NOTHING here.
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
    check(capHas(L"was NOT touched by this install"),
          L"C: it states the port was NOT touched");

    // -------------------------------------------------------------------
    // Branch D: nothing changed under a running application.
    // -------------------------------------------------------------------
    wprintf(L"\n-- branch D: nothing that needs a restart --\n");
    Pending d;
    ZeroMemory(&d, sizeof(d));
    d.teVirtualMidiMissing = true;
    capReset();
    printSummary(&s, &d, false, L"");
    check(!capHas(L"Close your DJ software and open it again"),
          L"D: no restart item");
    // NOT "the port is never mentioned": the standing warnings block DOES talk
    // about the port, and has to - item 3 is the rule about not ending the control
    // service. What must not appear is a CLAIM ABOUT THIS RUN. That distinction is
    // the whole of C1, so the check is written on the two per run sentences and not
    // on the words "virtual MIDI port".
    check(!capHas(L"stopping it DESTROYS the virtual MIDI port"),
          L"D: no per run claim that the port was destroyed");
    check(!capHas(L"was NOT touched by this install"),
          L"D: and no per run claim about the port either way");
    check(capHas(L"owns the virtual MIDI port for as long as it runs"),
          L"D: the standing rule about the port is still there");
    check(capHas(L"Install loopMIDI"),
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
          capHas(L"stopping it DESTROYS the virtual MIDI port"),
          L"E: all three reasons are given");
    check(capHas(L"regsvr32 \"C:\\Other\\Old.dll\""),
          L"E: the way back is still printed on failure");
    check(capHas(L"NEXT STEPS AND WARNINGS"),
          L"E: the warnings block is printed on a FAILED install too");

    bcdsetup::setLineSink(0);
    bcdsetup::setConsoleEcho(true);
}

// ===========================================================================
// PART 2 - the eight warnings, on the real printNextStepsAndWarnings()
//
// THE LABELS BELOW SAY "1/8", AND THAT NUMBER IS PART OF WHAT IS UNDER TEST. They
// said "1/7" until item 8 was added, and a label is not checked by anything: it
// would have gone on printing green while describing a block that had eight items.
// That is the defect class this folder exists to catch, so the count itself is now
// checked twice - the new wording has to be present and the old wording has to be
// GONE, here and inside the built binary (mode "exe").
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
    check(capHas(L"teVirtualMIDI is somebody else's software"),
          L"6/8 teVirtualMIDI is third party and not redistributed");
    check(capHas(L"WinUSB binding is a requirement"),
          L"7/8 the WinUSB binding, and Zadig last");
    check(capHas(L"never the"), L"7/8 says Zadig is the last thing to try");
    check(capHas(L"Nothing this installer does needs Windows to be restarted"),
          L"8/8 nothing here needs Windows restarted");
    check(capHas(L"LoadLibrary") && capHas(L"no kernel") &&
          capHas(L"nothing that loads at boot time"),
          L"8/8 gives the structural reason, not a reassurance");
    // *** THE SCOPE OF ITEM 8 IS THE POINT OF ITEM 8. *** It is a claim of absence,
    // and items 6 and 7 point at two third party installers that may legitimately
    // ask for a restart. A block that says "nothing needs a restart" while also
    // telling the reader to run Zadig and loopMIDI contradicts itself, and a reader
    // who catches one warnings block lying stops believing the other seven items.
    check(capHas(L"TWO OTHER THINGS CAN ASK FOR A RESTART") &&
          capHas(L"neither of them is this") &&
          capHas(L"Zadig (7)") && capHas(L"loopMIDI"),
          L"8/8 does NOT contradict 6 and 7: it names them as things that CAN ask");
    // The count in the first line, both ways round. The "absent" half is the half
    // that matters: it is what an added item nine would trip over.
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
// PART 3 - the pages, rendered, at two DPIs, on a private desktop
// ===========================================================================
namespace bcdgui {

static const wchar_t* g_shotDir = 0;

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
    switch (page) {
    case kPgWelcome: renderWelcome(mem, pw, ph, false); break;
    case kPgReview:  renderReview(mem, pw, ph, false);  break;
    case kPgWork:    renderWorkChrome(mem, pw, false);  break;
    case kPgDone:    renderDoneChrome(mem, pw, false);  break;
    }
    // The ink box is taken BEFORE the children go on, because the question it
    // answers is about the PAINTED text - the only text in this program that a
    // window can clip. The panes are windows and cannot be clipped by the page:
    // they wrap and they scroll.
    InkBox ink = inkOf(bmp, pw, surfaceH);
    if (withChildren) {
        if (page == kPgWork) {
            printChildInto(mem, g_bar);
            printChildInto(mem, g_log);
        } else if (page == kPgDone) {
            printChildInto(mem, g_summary);
        }
    }
    if (png)
        savePng(bmp, pw, surfaceH, png);
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
// moment it stops short, at any DPI and in any face, with no number to keep in step.
// ===========================================================================
static bool hasDescender(const wchar_t* s)
{
    for (; s && *s; s++)
        if (*s == L'g' || *s == L'j' || *s == L'p' || *s == L'q' || *s == L'y')
            return true;
    return false;
}

// Where the ink of one line really ends when nothing can clip it.
static int unclippedInkBottom(HFONT font, const wchar_t* text, RECT box,
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
    return ink.bottom;
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

    int titleWant = unclippedInkBottom(g_fHead, wiz->headline, hb.title,
                                       CLR_HEAD_TXT, CLR_HEAD_BG);
    int subWant   = unclippedInkBottom(g_fHeadSmall, wiz->subhead, hb.sub,
                                       CLR_HEAD_DIM, CLR_HEAD_BG);

    wchar_t png[512];
    _snwprintf(png, 500, L"%s\\header-band-%ddpi.png", g_shotDir, dpi);
    savePng(bmp, cw, surfaceH, png);

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

static const wchar_t* pageName(int p)
{
    switch (p) {
    case kPgWelcome: return L"1-welcome";
    case kPgReview:  return L"2-checks";
    case kPgWork:    return L"3-progress";
    default:         return L"4-finished";
    }
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
    SendMessageW(g_log, EM_SETLIMITTEXT, 0, 0);
    SendMessageW(g_summary, EM_SETLIMITTEXT, 0, 0);
    applyFonts();
    if (g_early && g_early[0])
        SetWindowTextW(g_log, g_early);
    if (wiz->showDevicePhoto)
        decodePhoto(S(kPhotoW));

    RECT want = { 0, 0, S(kWinW), S(kWinH) };
    AdjustWindowRectEx(&want, (DWORD)GetWindowLongPtrW(frame, GWL_STYLE), FALSE,
                       (DWORD)GetWindowLongPtrW(frame, GWL_EXSTYLE));
    SetWindowPos(frame, 0, 0, 0, want.right - want.left, want.bottom - want.top,
                 SWP_NOZORDER);

    // The last page's pane, filled with the REAL summary text - the same call
    // runSteps() makes, with the branch that the owner's machine will take on a
    // second install: the file replaced, the service left alone.
    {
        bcdsetup::MachineState s;
        fakeState(&s);
        Pending p;
        ZeroMemory(&p, sizeof(p));
        p.driverFileReplaced = true;
        capReset();
        bcdsetup::setLineSink(capSink);
        bcdsetup::setConsoleEcho(false);
        printSummary(&s, &p, false, L"");
        bcdsetup::setLineSink(0);
        bcdsetup::setConsoleEcho(true);
        for (int i = 0; i < g_capN; i++)
            appendTo(g_summary, g_cap[i]);
        SendMessageW(g_summary, EM_SETSEL, 0, 0);
        SendMessageW(g_summary, EM_SCROLLCARET, 0, 0);
    }

    ShowWindow(frame, SW_SHOW);

    // The band first: it is painted on every page, it is the same band on every
    // page, and it is measured once per DPI rather than four times over.
    measureHeadBand(wiz, dpi);

    for (int page = kPgWelcome; page <= kPgDone; page++) {
        if (page == kPgDone)
            g_outcome = kOutcomeOk;
        setPage(page);
        UpdateWindow(g_page);
        UpdateWindow(frame);

        RECT p;
        GetClientRect(g_page, &p);
        int pw = p.right, ph = p.bottom;

        // What the renderer says it needs.
        HDC dc = GetDC(g_page);
        int reported = 0;
        switch (page) {
        case kPgWelcome: reported = renderWelcome(dc, pw, ph, true); break;
        case kPgReview:  reported = renderReview(dc, pw, ph, true);  break;
        case kPgWork:    reported = renderWorkChrome(dc, pw, true);  break;
        default:         reported = renderDoneChrome(dc, pw, true);  break;
        }
        ReleaseDC(g_page, dc);

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
        SCROLLINFO si;
        ZeroMemory(&si, sizeof(si));
        si.cbSize = sizeof(si);
        si.fMask  = SIF_RANGE | SIF_PAGE;
        GetScrollInfo(g_page, SB_VERT, &si);
        bool reachable = (reported <= ph) ||
                         (si.nMax >= reported - 1 && (int)si.nPage >= ph);
        _snwprintf(what, 250,
                   L"%ddpi %s: all of it reachable (fits=%d, range=%d..%d page=%u)",
                   dpi, pageName(page), reported <= ph ? 1 : 0, si.nMin, si.nMax,
                   si.nPage);
        check(reachable, what);

        if (page == kPgDone) {
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
    }

    DestroyWindow(frame);
    g_frame = 0;
    g_page  = 0;
    releasePhoto();
}

// The name setup.cpp calls. It renders instead of looping.
int runWizard(Wizard* wiz)
{
    wprintf(L"\n=== PART 3: the four pages, rendered, 96 and 144 DPI ===\n");
    shootAtDpi(wiz, 96);
    shootAtDpi(wiz, 144);
    releaseFonts();
    return 0;
}

}   // namespace bcdgui

// ===========================================================================
// PART 4 - the words inside a BUILT executable, with a count of its own
//
// WHY THIS IS NOT IN "all", AND THEREFORE NOT IN THE 103. It needs a built
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
    check(exeHas(d, n, L"TWO OTHER THINGS CAN ASK FOR A RESTART"),
          L"and so is the sentence that keeps item 8 from contradicting 6 and 7");
    // The one character in this folder that is written as an escape. Reading it back
    // out of the binary is what proves the escape survived the compiler's idea of
    // the source's code page - which is the whole reason it is an escape.
    check(exeHas(d, n, L"Voc\u00EA precisa instalar alguns drivers primeiro"),
          L"the quoted VirtualDJ band is in the binary with U+00EA intact");

    HeapFree(GetProcessHeap(), 0, d);
}

// ===========================================================================
int wmain(int argc, wchar_t** argv)
{
    const wchar_t* mode = argc > 1 ? argv[1] : L"all";
    bcdgui::g_shotDir   = argc > 2 ? argv[2] : L".";

    wprintf(L"BCD3000 installer rounds 2 and 3 - verification harness\n");
    wprintf(L"nothing below installs, registers, writes or shows a pixel\n");

    if (_wcsicmp(mode, L"text") == 0 || _wcsicmp(mode, L"all") == 0) {
        testSummaryText();
        testWarningsBlock();
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

    if (_wcsicmp(mode, L"shots") == 0 || _wcsicmp(mode, L"all") == 0) {
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
                int            stop    = 0;
                const wchar_t* blocked = 0;
                bool           canRun  = prepare(&g_run, &stop, &blocked);
                runWindowed(&g_run, canRun ? 0 : blocked, stop);
                bcdsetup::setLineSink(0);
                bcdsetup::setAskHook(0);
            }
            SetThreadDesktop(old);
            CloseDesktop(desk);
        }
    }

    wprintf(L"\n===============================================================\n");
    wprintf(L"%d checks, %d failures\n", g_checks, g_fails);
    wprintf(L"%s\n", g_fails == 0 ? L"VERIFY_OK" : L"VERIFY_FAIL");
    return g_fails == 0 ? 0 : 1;
}
