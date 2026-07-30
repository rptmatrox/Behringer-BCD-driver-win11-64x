// The window, and nothing else. See gui.h for what this file is not allowed to
// know.
//
// WHAT IS DRAWN BY HAND AND WHAT IS A CONTROL, and why the split is where it is:
// the two panes that carry the installer's own words - the running log and the
// final summary - are real EDIT controls, so that the text can be selected,
// scrolled and copied into a support request with Ctrl+A, Ctrl+C. Everything else
// on the page (the header, the device photograph, the marks against each check
// and each step) is painted, because those need exact placement at any DPI and
// because a static control cannot be given a coloured tick.
//
// The cost of that choice is stated rather than hidden: the painted text is not
// visible to a screen reader. What a screen reader can reach is the window title,
// the buttons, and the two edit panes - and the two edit panes contain the whole
// of what the installer said, character for character, because they are fed from
// the same say*() calls the console prints. A user who cannot see the page can
// still read everything the page is about, and /console remains the mode with no
// pictures in it at all.

#include "gui.h"

#include <commctrl.h>
#include <wincodec.h>
#include <objbase.h>
#include <stdio.h>
#include <wchar.h>
#include <stdarg.h>

using namespace bcdsetup;

namespace bcdgui {

const wchar_t* const kNotAffiliatedLine1 =
    L"This is an independent, unofficial project. It is not affiliated with, "
    L"endorsed by or connected to Behringer or Music Tribe in any way.";
const wchar_t* const kNotAffiliatedLine2 =
    L"The name of the device is used only to say which hardware this driver "
    L"works with. All trademarks belong to their owners.";

const wchar_t* const kCreditsLine =
    L"by MatroX - independent Brazilian developer";
const wchar_t* const kRepositoryUrl =
    L"https://github.com/rptmatrox/Behringer-BCD-driver-win11-64x";

// ---------------------------------------------------------------------------
// Messages from the worker thread. The worker posts; the window's own thread is
// the only one that ever touches a control.
// ---------------------------------------------------------------------------
#define WM_BCD_LINE (WM_APP + 1)   // wParam: kind + kSummaryBit. lParam: heap text
#define WM_BCD_STEP (WM_APP + 2)   // wParam: row index. lParam: heap StepMsg
#define WM_BCD_DONE (WM_APP + 3)   // wParam: exit code. lParam: Outcome
#define WM_BCD_ASK  (WM_APP + 4)   // SENT, not posted. lParam: the question

// WM_BCD_LINE and WM_BCD_STEP have to stay adjacent and in this order: they are
// drained as one range before a question is put on the screen, so that the lines
// that explain the question are already visible behind it.

const WPARAM kSummaryBit = 0x100;

// This project does not define UNICODE, so IDC_ARROW expands to the ANSI flavour
// of MAKEINTRESOURCE and cannot be handed to LoadCursorW. 32512 is IDC_ARROW.
#define BCD_CURSOR_ARROW MAKEINTRESOURCEW(32512)

#define IDC_PRIMARY   1001
#define IDC_SECONDARY 1002
#define IDC_LOG       1003
#define IDC_SUMMARY   1004
#define IDC_BAR       1005

enum { kPgWelcome = 0, kPgReview = 1, kPgWork = 2, kPgDone = 3 };

// ---------------------------------------------------------------------------
// Colours. A light window on purpose: this thing runs once, before anything is
// configured, and a light installer is what Windows' own ones look like. There
// is no dark variant, and pretending to have one that was never looked at on a
// dark machine would be worse than not having one.
// ---------------------------------------------------------------------------
#define CLR_HEAD_BG   RGB(0x1C, 0x27, 0x33)
#define CLR_HEAD_TXT  RGB(0xFF, 0xFF, 0xFF)
#define CLR_HEAD_DIM  RGB(0x9E, 0xAF, 0xBF)
#define CLR_ACCENT    RGB(0x2E, 0x8B, 0xC8)
#define CLR_PAGE_BG   RGB(0xFF, 0xFF, 0xFF)
#define CLR_PANEL_BG  RGB(0xF7, 0xF8, 0xF9)
#define CLR_FOOT_BG   RGB(0xF1, 0xF2, 0xF3)
#define CLR_LINE      RGB(0xDA, 0xDD, 0xE0)
#define CLR_TEXT      RGB(0x1F, 0x1F, 0x1F)
#define CLR_DIM       RGB(0x5C, 0x5C, 0x5C)
#define CLR_FAINT     RGB(0x7A, 0x7A, 0x7A)
#define CLR_OK        RGB(0x0E, 0x70, 0x0E)
#define CLR_WARN      RGB(0x9A, 0x5B, 0x00)
#define CLR_FAIL      RGB(0xC4, 0x2B, 0x1C)
#define CLR_BUSY      RGB(0x00, 0x5A, 0x9E)
#define CLR_GREY      RGB(0xA0, 0xA4, 0xA8)

// Layout, in logical pixels at 96 DPI. Everything is scaled through S().
const int kWinW      = 700;
const int kWinH      = 540;
// kHeadH is a FLOOR, not the height: headBand() works the real height out from the
// metrics of the two fonts that draw the band. See the comment there.
const int kHeadH     = 70;
const int kHeadPadTop    = 12;   // above the headline
const int kHeadGap       = 2;    // between the headline and the subhead
const int kHeadPadBottom = 8;    // between the subhead and the rule that closes it
const int kFootH     = 60;
const int kMargin    = 26;
const int kBtnW      = 122;
const int kBtnH      = 32;
const int kPhotoW    = 210;
const int kMarkSize  = 15;

// ---------------------------------------------------------------------------
// State. Plain handles and plain data: no object at file scope has a destructor
// in this project, and a window that ran code at process unload would be the
// first one.
// ---------------------------------------------------------------------------
static HINSTANCE g_inst        = 0;
static HWND      g_frame       = 0;
static HWND      g_page        = 0;
static HWND      g_primary     = 0;
static HWND      g_secondary   = 0;
static HWND      g_log         = 0;
static HWND      g_summary     = 0;
static HWND      g_bar         = 0;
static Wizard*   g_wiz         = 0;

static int       g_dpi         = 96;
static HFONT     g_fTitle      = 0;
static HFONT     g_fHead       = 0;
static HFONT     g_fHeadSmall  = 0;
static HFONT     g_fBody       = 0;
static HFONT     g_fBodyBold   = 0;
static HFONT     g_fSmall      = 0;
static HFONT     g_fMono       = 0;
static wchar_t   g_faceName[LF_FACESIZE];

static HBITMAP   g_photo       = 0;
static int       g_photoW      = 0;
static int       g_photoH      = 0;

static int       g_pageIndex   = kPgWelcome;
static int       g_scrollY     = 0;
static int       g_contentH    = 0;
static bool      g_working     = false;
static bool      g_finished    = false;
static int       g_exitCode    = 0;
static Outcome   g_outcome     = kOutcomeFailed;
static HANDLE    g_worker      = 0;

// Written and read on the worker thread only.
static bool      g_capture     = false;
static Outcome   g_workerOutcome = kOutcomeFailed;

static int S(int logical) { return MulDiv(logical, g_dpi, 96); }

// ---------------------------------------------------------------------------
// Rows
// ---------------------------------------------------------------------------
void setRow(Row* row, RowState state, const wchar_t* title, const wchar_t* detailFmt, ...)
{
    ZeroMemory(row, sizeof(*row));
    row->state = state;
    if (title) {
        wcsncpy(row->title, title, kRowTitle - 1);
        row->title[kRowTitle - 1] = 0;
    }
    if (detailFmt) {
        va_list ap;
        va_start(ap, detailFmt);
        _vsnwprintf(row->detail, kRowText - 1, detailFmt, ap);
        va_end(ap);
        row->detail[kRowText - 1] = 0;
    }
}

// ---------------------------------------------------------------------------
// The device photograph.
//
// ROUTE TAKEN: the PNG is embedded byte for byte and decoded here with WIC, the
// image decoder that is part of Windows. The alternative was converting it to a
// BMP during the build with ffmpeg, and it was rejected for three reasons that
// all point the same way:
//
//   1. it would put a third party tool in the build. This project has just
//      finished removing one from the build on purpose; adding a different one
//      back for a picture would be an odd trade.
//   2. it would create a generated file that has to be kept in step with
//      docs\BCD3000.PNG - either committed, which puts a build output in the
//      repository, or rebuilt by everyone who clones, which needs the tool.
//      One file with one id is the whole point.
//   3. a 32 bit BMP's alpha channel is not reliably honoured by LoadImage, so
//      the practical BMP route is to flatten the transparency onto a chosen
//      background colour at build time. The picture would then only be correct
//      on that exact colour, and the page's background would no longer be free
//      to change.
//
// WIC also scales at decode time, so the bitmap is produced at exactly the pixel
// size the current monitor needs, with a proper resampling filter, instead of
// being stretched afterwards by AlphaBlend. That is what keeps it sharp on a high
// density screen, and it is why the bitmap is thrown away and decoded again when
// the window moves to a monitor with a different DPI.
// ---------------------------------------------------------------------------
static void releasePhoto()
{
    if (g_photo) {
        DeleteObject(g_photo);
        g_photo = 0;
    }
    g_photoW = g_photoH = 0;
}

static bool decodePhoto(int targetW)
{
    releasePhoto();

    const void* data = 0;
    DWORD       size = 0;
    if (!loadPayload(IDR_DEVICE_PHOTO_PNG, &data, &size) || !data || !size)
        return false;

    IWICImagingFactory*  factory = 0;
    IWICStream*          stream  = 0;
    IWICBitmapDecoder*   decoder = 0;
    IWICBitmapFrameDecode* frame = 0;
    IWICFormatConverter* conv    = 0;
    IWICBitmapScaler*    scaler  = 0;
    bool                 ok      = false;

    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, 0, CLSCTX_INPROC_SERVER,
                                  IID_IWICImagingFactory, (void**)&factory);
    if (SUCCEEDED(hr) && factory)
        hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr) && stream)
        hr = stream->InitializeFromMemory((BYTE*)const_cast<void*>(data), size);
    if (SUCCEEDED(hr))
        hr = factory->CreateDecoderFromStream(stream, 0, WICDecodeMetadataCacheOnLoad,
                                              &decoder);
    if (SUCCEEDED(hr) && decoder)
        hr = decoder->GetFrame(0, &frame);

    UINT srcW = 0, srcH = 0;
    if (SUCCEEDED(hr) && frame)
        hr = frame->GetSize(&srcW, &srcH);
    if (SUCCEEDED(hr) && (srcW == 0 || srcH == 0))
        hr = E_FAIL;

    // Premultiply FIRST and scale afterwards. Resampling a non premultiplied
    // image mixes the colour of fully transparent pixels into its neighbours,
    // which shows up as a pale halo around the edge of the device.
    if (SUCCEEDED(hr))
        hr = factory->CreateFormatConverter(&conv);
    if (SUCCEEDED(hr) && conv)
        hr = conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                              WICBitmapDitherTypeNone, 0, 0.0f,
                              WICBitmapPaletteTypeCustom);

    UINT dstW = (UINT)targetW;
    UINT dstH = 1;
    if (SUCCEEDED(hr)) {
        dstH = (UINT)MulDiv((int)dstW, (int)srcH, (int)srcW);
        if (dstH < 1)
            dstH = 1;
        hr = factory->CreateBitmapScaler(&scaler);
    }
    if (SUCCEEDED(hr) && scaler)
        hr = scaler->Initialize(conv, dstW, dstH, WICBitmapInterpolationModeFant);

    if (SUCCEEDED(hr)) {
        BITMAPINFO bi;
        ZeroMemory(&bi, sizeof(bi));
        bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth       = (LONG)dstW;
        bi.bmiHeader.biHeight      = -(LONG)dstH;    // top down
        bi.bmiHeader.biPlanes      = 1;
        bi.bmiHeader.biBitCount    = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        void*   bits = 0;
        HBITMAP bmp  = CreateDIBSection(0, &bi, DIB_RGB_COLORS, &bits, 0, 0);
        if (bmp && bits) {
            UINT stride = dstW * 4;
            hr = scaler->CopyPixels(0, stride, stride * dstH, (BYTE*)bits);
            if (SUCCEEDED(hr)) {
                g_photo  = bmp;
                g_photoW = (int)dstW;
                g_photoH = (int)dstH;
                ok       = true;
            } else {
                DeleteObject(bmp);
            }
        }
    }

    if (scaler)  scaler->Release();
    if (conv)    conv->Release();
    if (frame)   frame->Release();
    if (decoder) decoder->Release();
    if (stream)  stream->Release();
    if (factory) factory->Release();
    return ok;
}

// ---------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------
static HFONT makeFont(int tenthsOfAPoint, int weight, const wchar_t* face)
{
    LOGFONTW lf;
    ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight         = -MulDiv(tenthsOfAPoint, g_dpi, 720);
    lf.lfWeight         = weight;
    lf.lfCharSet        = DEFAULT_CHARSET;
    lf.lfOutPrecision   = OUT_TT_PRECIS;
    lf.lfQuality        = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    wcsncpy(lf.lfFaceName, face, LF_FACESIZE - 1);
    lf.lfFaceName[LF_FACESIZE - 1] = 0;
    return CreateFontIndirectW(&lf);
}

static void releaseFonts()
{
    HFONT* all[7] = { &g_fTitle, &g_fHead, &g_fHeadSmall, &g_fBody,
                      &g_fBodyBold, &g_fSmall, &g_fMono };
    for (int i = 0; i < 7; i++) {
        if (*all[i]) {
            DeleteObject(*all[i]);
            *all[i] = 0;
        }
    }
}

static void buildFonts()
{
    releaseFonts();
    g_fHead      = makeFont(165, FW_SEMIBOLD, g_faceName);
    g_fHeadSmall = makeFont(90,  FW_NORMAL,   g_faceName);
    g_fTitle     = makeFont(125, FW_SEMIBOLD, g_faceName);
    g_fBody      = makeFont(97,  FW_NORMAL,   g_faceName);
    g_fBodyBold  = makeFont(97,  FW_SEMIBOLD, g_faceName);
    g_fSmall     = makeFont(85,  FW_NORMAL,   g_faceName);
    // The log is the console's text. It reads as the console's text when it is
    // laid out like it, and the installer's output is full of aligned prefixes.
    g_fMono      = makeFont(85,  FW_NORMAL,   L"Consolas");
}

static void applyFonts()
{
    if (g_primary)   SendMessageW(g_primary,   WM_SETFONT, (WPARAM)g_fBody, TRUE);
    if (g_secondary) SendMessageW(g_secondary, WM_SETFONT, (WPARAM)g_fBody, TRUE);
    if (g_log)       SendMessageW(g_log,       WM_SETFONT, (WPARAM)g_fMono, TRUE);
    if (g_summary)   SendMessageW(g_summary,   WM_SETFONT, (WPARAM)g_fMono, TRUE);
}

// ---------------------------------------------------------------------------
// Painting helpers
// ---------------------------------------------------------------------------
static void fillRect(HDC dc, int x, int y, int w, int h, COLORREF c)
{
    RECT  r = { x, y, x + w, y + h };
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

// Draws a wrapped block and returns its height. When measure is true nothing is
// drawn, which is how the scrolling page finds out how tall it is.
static int textBlock(HDC dc, HFONT font, COLORREF colour, const wchar_t* text,
                     int x, int y, int w, bool measure)
{
    if (!text || !*text)
        return 0;
    RECT r = { x, y, x + w, y + S(4000) };
    SelectObject(dc, font);
    DrawTextW(dc, text, -1, &r, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    int h = r.bottom - r.top;
    if (!measure) {
        RECT d = { x, y, x + w, y + h };
        SetTextColor(dc, colour);
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, text, -1, &d, DT_WORDBREAK | DT_NOPREFIX);
    }
    return h;
}

static int lineHeight(HDC dc, HFONT font)
{
    TEXTMETRICW tm;
    SelectObject(dc, font);
    GetTextMetricsW(dc, &tm);
    return tm.tmHeight;
}

static COLORREF markColour(RowState st)
{
    switch (st) {
    case kRowOk:      return CLR_OK;
    case kRowWarn:    return CLR_WARN;
    case kRowFail:    return CLR_FAIL;
    case kRowBusy:    return CLR_BUSY;
    case kRowSkipped: return CLR_GREY;
    case kRowWaiting: return CLR_GREY;
    default:          return CLR_GREY;
    }
}

// The mark against a check or a step. Drawn with GDI primitives rather than with
// a character, so that it cannot depend on which glyphs a font happens to have.
static void drawMark(HDC dc, int x, int y, int size, RowState st)
{
    COLORREF c   = markColour(st);
    int      pen = S(2) < 1 ? 1 : S(2);

    if (st == kRowNeutral) {
        int    d  = size / 2;
        int    o  = (size - d) / 2;
        HBRUSH br = CreateSolidBrush(CLR_GREY);
        HPEN   pn = CreatePen(PS_SOLID, 1, CLR_GREY);
        HGDIOBJ ob = SelectObject(dc, br);
        HGDIOBJ op = SelectObject(dc, pn);
        Ellipse(dc, x + o, y + o, x + o + d, y + o + d);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(br);
        DeleteObject(pn);
        return;
    }

    bool    hollow = (st == kRowWaiting);
    HBRUSH  br     = CreateSolidBrush(hollow ? CLR_PAGE_BG : c);
    HPEN    pn     = CreatePen(PS_SOLID, pen, c);
    HGDIOBJ ob     = SelectObject(dc, br);
    HGDIOBJ op     = SelectObject(dc, pn);
    Ellipse(dc, x, y, x + size, y + size);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pn);

    if (hollow)
        return;

    HPEN    wp = CreatePen(PS_SOLID, pen, RGB(0xFF, 0xFF, 0xFF));
    HGDIOBJ ow = SelectObject(dc, wp);
    int     q  = size / 4;
    if (st == kRowOk) {
        POINT p[3] = { { x + q, y + size / 2 },
                       { x + size / 2 - q / 3, y + size - q - pen / 2 },
                       { x + size - q, y + q + pen / 2 } };
        Polyline(dc, p, 3);
    } else if (st == kRowFail) {
        MoveToEx(dc, x + q, y + q, 0);
        LineTo(dc, x + size - q, y + size - q);
        MoveToEx(dc, x + size - q, y + q, 0);
        LineTo(dc, x + q, y + size - q);
    } else if (st == kRowWarn) {
        MoveToEx(dc, x + size / 2, y + q - pen / 2, 0);
        LineTo(dc, x + size / 2, y + size - 2 * q);
        MoveToEx(dc, x + size / 2, y + size - q - pen / 2, 0);
        LineTo(dc, x + size / 2, y + size - q);
    } else if (st == kRowSkipped) {
        MoveToEx(dc, x + q, y + size / 2, 0);
        LineTo(dc, x + size - q, y + size / 2);
    } else if (st == kRowBusy) {
        MoveToEx(dc, x + size / 2 - pen, y + size / 2, 0);
        LineTo(dc, x + size / 2 + pen, y + size / 2);
    }
    SelectObject(dc, ow);
    DeleteObject(wp);
}

// One row: the mark, the title, and the detail wrapped under it. Returns the
// height it used or would use.
static int drawRow(HDC dc, const Row* row, int x, int y, int w, bool measure,
                   bool compact)
{
    int mark   = S(kMarkSize);
    int indent = mark + S(12);
    int top    = y;

    if (!measure)
        drawMark(dc, x, y + S(2), mark, row->state);

    COLORREF titleColour = CLR_TEXT;
    if (row->state == kRowWaiting || row->state == kRowSkipped)
        titleColour = CLR_FAINT;
    y += textBlock(dc, compact ? g_fBody : g_fBodyBold, titleColour, row->title,
                   x + indent, y, w - indent, measure);
    if (row->detail[0]) {
        y += S(2);
        y += textBlock(dc, g_fSmall, CLR_DIM, row->detail, x + indent, y,
                       w - indent, measure);
    }
    y += compact ? S(8) : S(14);
    return y - top;
}

// ---------------------------------------------------------------------------
// The pages. Each renderer both measures and draws, so the scroll range and the
// picture can never disagree about how tall the content is.
// ---------------------------------------------------------------------------
static int renderWelcome(HDC dc, int w, int h, bool measure)
{
    int m       = S(kMargin);
    int y       = S(24);
    int textX   = m;
    int textW   = w - 2 * m;

    if (g_wiz->showDevicePhoto && g_photo) {
        int panelW = g_photoW + S(16);
        int panelH = g_photoH + S(16);
        if (!measure) {
            fillRect(dc, m, y, panelW, panelH, CLR_PANEL_BG);
            HDC   mem = CreateCompatibleDC(dc);
            HGDIOBJ ob = SelectObject(mem, g_photo);
            BLENDFUNCTION bf;
            bf.BlendOp             = AC_SRC_OVER;
            bf.BlendFlags          = 0;
            bf.SourceConstantAlpha = 255;
            bf.AlphaFormat         = AC_SRC_ALPHA;
            AlphaBlend(dc, m + S(8), y + S(8), g_photoW, g_photoH,
                       mem, 0, 0, g_photoW, g_photoH, bf);
            SelectObject(mem, ob);
            DeleteDC(mem);
        }
        textX = m + panelW + S(22);
        textW = w - m - textX;
    }

    int ty = y + S(4);
    ty += textBlock(dc, g_fTitle, CLR_TEXT, g_wiz->welcomeLine1, textX, ty, textW,
                    measure);
    ty += S(8);
    ty += textBlock(dc, g_fBody, CLR_DIM, g_wiz->welcomeLine2, textX, ty, textW,
                    measure);
    ty += S(14);
    for (int i = 0; i < 4 && g_wiz->welcomeBullets[i]; i++) {
        int bx = textX + S(14);
        if (!measure) {
            int lh = lineHeight(dc, g_fSmall);
            fillRect(dc, textX + S(2), ty + lh / 2 - S(2), S(4), S(4), CLR_ACCENT);
        }
        ty += textBlock(dc, g_fSmall, CLR_TEXT, g_wiz->welcomeBullets[i], bx, ty,
                        textW - S(14), measure);
        ty += S(6);
    }

    // THE FOOT OF THE PAGE: who wrote this, where it lives, and the fine print.
    //
    // Bottom anchored, so that it reads as the fine print - but it is on the FIRST
    // page and not behind a link. The credit is above the notice and in the page's
    // ordinary text colour, because it is a statement and not a disclaimer; the
    // repository address is in the accent colour so that it is findable, and it is
    // plain selectable-looking text rather than a link, because a link in an
    // elevated installer is a thing nobody should be inviting a user to click.
    //
    // MEASURED BEFORE IT IS PLACED, and that is what makes it safe at any DPI:
    // every line below goes through textBlock(), which wraps and returns the height
    // it used, and the bottom this function returns is what layout() turns into the
    // page's scroll range. So the foot can grow - a taller font, a narrower window,
    // a longer address - and the page gets a scrollbar instead of a cut sentence.
    // The floor "lowest" keeps it from ever climbing into the bullets or the photo.
    int fullW = w - 2 * m;
    int hCred = textBlock(dc, g_fSmall, CLR_TEXT, kCreditsLine, m, 0, fullW, true);
    int hRepo = textBlock(dc, g_fSmall, CLR_ACCENT, kRepositoryUrl, m, 0, fullW, true);
    int h1 = 0, h2 = 0;
    if (g_wiz->showNotAffiliated) {
        h1 = textBlock(dc, g_fSmall, CLR_FAINT, kNotAffiliatedLine1, m, 0, fullW, true);
        h2 = textBlock(dc, g_fSmall, CLR_FAINT, kNotAffiliatedLine2, m, 0, fullW, true);
    }
    int block = hCred + S(2) + hRepo;
    if (g_wiz->showNotAffiliated)
        block += S(10) + h1 + S(4) + h2;

    int footY  = h - block - S(20);
    int lowest = (ty > y + g_photoH + S(16) ? ty : y + g_photoH + S(16)) + S(18);
    if (footY < lowest)
        footY = lowest;
    if (!measure) {
        fillRect(dc, m, footY - S(14), fullW, 1, CLR_LINE);
        int ny = footY;
        ny += textBlock(dc, g_fSmall, CLR_TEXT, kCreditsLine, m, ny, fullW, false);
        ny += S(2);
        ny += textBlock(dc, g_fSmall, CLR_ACCENT, kRepositoryUrl, m, ny, fullW, false);
        if (g_wiz->showNotAffiliated) {
            ny += S(10);
            ny += textBlock(dc, g_fSmall, CLR_FAINT, kNotAffiliatedLine1, m, ny,
                            fullW, false);
            ny += S(4);
            textBlock(dc, g_fSmall, CLR_FAINT, kNotAffiliatedLine2, m, ny, fullW,
                      false);
        }
    }
    return footY + block + S(20);
}

static int renderReview(HDC dc, int w, int h, bool measure)
{
    (void)h;
    int m = S(kMargin);
    int y = S(22);

    y += textBlock(dc, g_fTitle, CLR_TEXT, g_wiz->reviewCaption, m, y, w - 2 * m,
                   measure);
    y += S(14);
    if (!measure)
        fillRect(dc, m, y, w - 2 * m, 1, CLR_LINE);
    y += S(14);

    for (int i = 0; i < g_wiz->reviewCount && i < kMaxRows; i++)
        y += drawRow(dc, &g_wiz->review[i], m, y, w - 2 * m, measure, false);

    if (g_wiz->reviewFooter) {
        y += S(4);
        if (!measure)
            fillRect(dc, m, y, w - 2 * m, 1, CLR_LINE);
        y += S(12);
        y += textBlock(dc, g_fSmall, CLR_DIM, g_wiz->reviewFooter, m, y,
                       w - 2 * m, measure);
    }
    return y + S(20);
}

// The work page's rows and caption. The two controls under them - the progress
// bar and the log pane - are real windows and are placed by layout().
static int renderWorkChrome(HDC dc, int w, bool measure)
{
    int m = S(kMargin);
    int y = S(18);
    y += textBlock(dc, g_fTitle, CLR_TEXT, g_wiz->progressCaption, m, y,
                   w - 2 * m, measure);
    y += S(12);
    if (!measure)
        fillRect(dc, m, y, w - 2 * m, 1, CLR_LINE);
    y += S(12);
    for (int i = 0; i < g_wiz->stepCount && i < kMaxRows; i++)
        y += drawRow(dc, &g_wiz->steps[i], m, y, w - 2 * m, measure, true);
    return y;
}

static int renderDoneChrome(HDC dc, int w, bool measure)
{
    int m = S(kMargin);
    int y = S(20);
    const wchar_t* caption = g_wiz->doneCaptionFail;
    COLORREF       colour  = CLR_FAIL;
    RowState       mark    = kRowFail;
    if (g_outcome == kOutcomeOk) {
        caption = g_wiz->doneCaptionOk;
        colour  = CLR_OK;
        mark    = kRowOk;
    } else if (g_outcome == kOutcomeStopped) {
        caption = g_wiz->doneCaptionStopped;
        colour  = CLR_WARN;
        mark    = kRowWarn;
    }
    int mk = S(kMarkSize + 5);
    if (!measure)
        drawMark(dc, m, y + S(3), mk, mark);
    y += textBlock(dc, g_fTitle, colour, caption, m + mk + S(12), y,
                   w - 2 * m - mk - S(12), measure);
    y += S(14);
    if (!measure)
        fillRect(dc, m, y, w - 2 * m, 1, CLR_LINE);
    y += S(12);

    // The one sentence on this page that must not need scrolling. It is a pointer
    // at the warnings in the summary pane below, never the warnings themselves -
    // see Wizard::doneNotice. Wrapped and measured here, so the pane below simply
    // starts lower when it takes two lines instead of one.
    if (g_wiz->doneNotice) {
        y += textBlock(dc, g_fBodyBold, CLR_WARN, g_wiz->doneNotice, m, y,
                       w - 2 * m, measure);
        y += S(10);
    }
    return y;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
// THE HEADER BAND'S GEOMETRY, TAKEN FROM THE FONTS THAT DRAW IT.
//
// It used to be four constants: the band 70 logical pixels tall, the headline in a
// box 26 tall at y=14, the subhead in a box 20 tall at y=42. DrawTextW CLIPS to the
// box it is given, and that box was SHORTER THAN THE FONT'S OWN CELL, so the tail
// of every descender was cut off. That is what the owner saw on the second real
// run: the g of "Behringer" with no tail.
//
// MEASURED, NOT GUESSED. Segoe UI at 16.5pt has tmHeight 30 at 96 DPI and 45 at
// 144, and the ink of "Behringer BCD3000 ASIO driver" ends on row 28 of that cell at
// 96 and row 43 at 144 - against boxes of 26 and 39. So it was cut at BOTH DPIs, by
// 3 rows and by 5, and a correction in fixed pixels would have had to be wrong at
// one of them.
//
// tmHeight is tmAscent + tmDescent: the whole cell, descenders included. A box that
// tall cannot clip anything the font puts on the line, in any face and at any DPI,
// and that is the whole reason the metric is asked for instead of a number being
// picked. installer/verify measures exactly this, by comparing the ink of the
// painted band against the ink the same font produces with nothing to clip it.
//
// kHeadH is now a FLOOR and no longer the height. At 96 DPI with the system message
// font the arithmetic below lands on exactly 70, so the band the owner has already
// seen does not move; at 144 it asks for 108 where 1.5 x 70 is 105, and it gets 108.
//
// ONE function, used by BOTH the painter and the layout. The band's height decides
// where the page begins, so a second copy of this arithmetic would be a painter and
// a layout that disagree about it - and the page would be painted over the rule or
// leave a stripe of nothing under it.
struct HeadBand {
    int  height;    // the whole band, including the rule that closes it
    int  accentH;   // that rule
    RECT title;     // the box DrawTextW is given for the headline
    RECT sub;       // and the one for the subhead
};

static HeadBand headBand(HDC dc, int cw)
{
    HeadBand    b;
    TEXTMETRICW tm;
    int         m = S(kMargin);
    ZeroMemory(&b, sizeof(b));
    b.accentH      = S(3);
    b.title.left   = m;
    b.title.right  = cw - m;
    b.title.top    = S(kHeadPadTop);
    SelectObject(dc, g_fHead);
    GetTextMetricsW(dc, &tm);
    b.title.bottom = b.title.top + tm.tmHeight;
    b.sub.left     = m;
    b.sub.right    = cw - m;
    b.sub.top      = b.title.bottom + S(kHeadGap);
    SelectObject(dc, g_fHeadSmall);
    GetTextMetricsW(dc, &tm);
    b.sub.bottom   = b.sub.top + tm.tmHeight;
    b.height       = b.sub.bottom + S(kHeadPadBottom) + b.accentH;
    if (b.height < S(kHeadH))
        b.height = S(kHeadH);
    return b;
}

static void showOnly(HWND wnd, bool visible)
{
    if (wnd)
        ShowWindow(wnd, visible ? SW_SHOW : SW_HIDE);
}

static void layout()
{
    if (!g_frame || !g_wiz)
        return;
    RECT c;
    GetClientRect(g_frame, &c);
    int w = c.right, h = c.bottom;
    // The same headBand() the painter uses, so the page starts exactly under the
    // rule that closes the band whatever the fonts turn out to need.
    HDC fdc   = GetDC(g_frame);
    int headH = headBand(fdc, w).height;
    ReleaseDC(g_frame, fdc);
    int footH = S(kFootH);
    int pageH = h - headH - footH;
    if (pageH < S(80))
        pageH = S(80);

    SetWindowPos(g_page, 0, 0, headH, w, pageH, SWP_NOZORDER | SWP_NOACTIVATE);

    int btnW = S(kBtnW), btnH = S(kBtnH);
    int by   = headH + pageH + (footH - btnH) / 2;
    int px   = w - S(24) - btnW;
    int sx   = px - S(10) - btnW;
    SetWindowPos(g_primary,   0, px, by, btnW, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g_secondary, 0, sx, by, btnW, btnH, SWP_NOZORDER | SWP_NOACTIVATE);

    // Inside the page.
    RECT p;
    GetClientRect(g_page, &p);
    int pw = p.right, ph = p.bottom;
    int m  = S(kMargin);

    HDC dc = GetDC(g_page);
    int chrome = 0;
    if (g_pageIndex == kPgWork)
        chrome = renderWorkChrome(dc, pw, true);
    else if (g_pageIndex == kPgDone)
        chrome = renderDoneChrome(dc, pw, true);

    g_contentH = 0;
    if (g_pageIndex == kPgWelcome)
        g_contentH = renderWelcome(dc, pw, ph, true);
    else if (g_pageIndex == kPgReview)
        g_contentH = renderReview(dc, pw, ph, true);
    ReleaseDC(g_page, dc);

    if (g_pageIndex == kPgWork) {
        int barH = S(8);
        int barY = chrome + S(6);
        SetWindowPos(g_bar, 0, m, barY, pw - 2 * m, barH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        int logY = barY + barH + S(12);
        int logH = ph - logY - S(16);
        if (logH < S(40))
            logH = S(40);
        SetWindowPos(g_log, 0, m, logY, pw - 2 * m, logH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (g_pageIndex == kPgDone) {
        int sy = chrome + S(6);
        int sh = ph - sy - S(16);
        if (sh < S(40))
            sh = S(40);
        SetWindowPos(g_summary, 0, m, sy, pw - 2 * m, sh,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // Only the review page can grow past the window, and only it scrolls.
    SCROLLINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = (g_contentH > 0 ? g_contentH - 1 : 0);
    si.nPage  = (UINT)ph;
    if (g_scrollY > g_contentH - ph)
        g_scrollY = g_contentH - ph;
    if (g_scrollY < 0)
        g_scrollY = 0;
    si.nPos = g_scrollY;
    SetScrollInfo(g_page, SB_VERT, &si, TRUE);
    ShowScrollBar(g_page, SB_VERT, g_contentH > ph);
}

static void refreshButtons()
{
    const wchar_t* primary   = L"Close";
    const wchar_t* secondary = L"Cancel";
    bool showPrimary   = true;
    bool showSecondary = true;
    bool enablePrimary = true;

    switch (g_pageIndex) {
    case kPgWelcome:
        primary   = g_wiz->startVerb;
        secondary = L"Cancel";
        break;
    case kPgReview:
        primary   = g_wiz->startVerb;
        secondary = g_wiz->hasWelcome ? L"Back" : L"Cancel";
        if (g_wiz->startBlockedNote)
            enablePrimary = false;
        break;
    case kPgWork:
        // The start button is gone rather than greyed: it has already been
        // pressed, and offering it again in any state invites a second press.
        showPrimary   = false;
        enablePrimary = false;
        secondary     = L"Cancel";
        break;
    case kPgDone:
        primary       = L"Close";
        showSecondary = false;
        break;
    }

    SetWindowTextW(g_primary, primary);
    SetWindowTextW(g_secondary, secondary);
    EnableWindow(g_primary, enablePrimary ? TRUE : FALSE);
    showOnly(g_primary, showPrimary);
    // On the work page the Cancel button stays visible and disabled, next to a
    // sentence that says why. Hiding it would be tidier and would leave the user
    // guessing whether cancelling is possible; a greyed button beside "this
    // cannot be stopped" answers the question without promising anything.
    EnableWindow(g_secondary, g_pageIndex == kPgWork ? FALSE : TRUE);
    showOnly(g_secondary, showSecondary);
    if (enablePrimary)
        SetFocus(g_primary);
}

static void setPage(int page)
{
    g_pageIndex = page;
    g_scrollY   = 0;
    showOnly(g_bar,     page == kPgWork);
    showOnly(g_log,     page == kPgWork);
    showOnly(g_summary, page == kPgDone);
    refreshButtons();
    layout();
    InvalidateRect(g_page, 0, TRUE);
    InvalidateRect(g_frame, 0, FALSE);
}

// ---------------------------------------------------------------------------
// The worker thread and the three things it is allowed to say
// ---------------------------------------------------------------------------
struct StepMsg {
    RowState state;
    wchar_t  detail[kRowText];
};

// Lines that arrived before there was a window to put them in. See
// beginCapture() in gui.h for why they exist. Touched by the main thread only:
// everything before the window is created runs there, and after that sinkLine()
// takes the posting path instead.
static wchar_t* g_early     = 0;
static SIZE_T   g_earlyUsed = 0;   // characters, not counting the terminator
static SIZE_T   g_earlyCap  = 0;

static void earlyAppend(const wchar_t* prefix, const wchar_t* body)
{
    SIZE_T need = g_earlyUsed + wcslen(prefix) + wcslen(body) + 3;
    if (need > g_earlyCap) {
        SIZE_T cap = g_earlyCap ? g_earlyCap * 2 : 8192;
        while (cap < need)
            cap *= 2;
        wchar_t* grown = g_early
            ? (wchar_t*)HeapReAlloc(GetProcessHeap(), 0, g_early, cap * sizeof(wchar_t))
            : (wchar_t*)HeapAlloc(GetProcessHeap(), 0, cap * sizeof(wchar_t));
        if (!grown)
            return;   // out of memory: the log file still has every line
        if (!g_early)
            grown[0] = 0;
        g_early    = grown;
        g_earlyCap = cap;
    }
    wcscat(g_early, prefix);
    wcscat(g_early, body);
    wcscat(g_early, L"\r\n");
    g_earlyUsed = wcslen(g_early);
}

static void releaseEarly()
{
    if (g_early) {
        HeapFree(GetProcessHeap(), 0, g_early);
        g_early = 0;
    }
    g_earlyUsed = g_earlyCap = 0;
}

static void sinkLine(LineKind kind, const wchar_t* prefix, const wchar_t* body)
{
    if (!g_frame) {
        earlyAppend(prefix, body);
        return;
    }
    SIZE_T n = (wcslen(prefix) + wcslen(body) + 3) * sizeof(wchar_t);
    wchar_t* buf = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, n);
    if (!buf)
        return;
    wcscpy(buf, prefix);
    wcscat(buf, body);
    WPARAM w = (WPARAM)kind | (g_capture ? kSummaryBit : 0);
    if (!PostMessageW(g_frame, WM_BCD_LINE, w, (LPARAM)buf))
        HeapFree(GetProcessHeap(), 0, buf);
}

static bool askViaWindow(const wchar_t* question)
{
    if (!g_frame)
        return false;
    return SendMessageW(g_frame, WM_BCD_ASK, 0, (LPARAM)question) != 0;
}

void postStep(int index, RowState state, const wchar_t* detail)
{
    if (!g_frame || index < 0 || index >= kMaxRows)
        return;
    StepMsg* m = (StepMsg*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(StepMsg));
    if (!m)
        return;
    m->state = state;
    if (detail) {
        wcsncpy(m->detail, detail, kRowText - 1);
        m->detail[kRowText - 1] = 0;
    }
    if (!PostMessageW(g_frame, WM_BCD_STEP, (WPARAM)index, (LPARAM)m))
        HeapFree(GetProcessHeap(), 0, m);
}

void beginSummaryCapture()
{
    g_capture = true;
}

void postOutcome(Outcome outcome)
{
    g_workerOutcome = outcome;
}

// The whole of the install runs here. It never touches a window: the only calls
// out of it are say*() - which reaches the window through sinkLine() above -
// askYesNo(), and postStep(). All three post or send a message and return.
static DWORD WINAPI workerProc(LPVOID)
{
    int code = g_wiz->work(g_wiz->user);
    // Read on this thread and handed over as a message parameter rather than
    // left in a global for the other thread to pick up, so there is no question
    // about when it becomes visible over there.
    PostMessageW(g_frame, WM_BCD_DONE, (WPARAM)code, (LPARAM)g_workerOutcome);
    return 0;
}

static void appendTo(HWND edit, const wchar_t* text)
{
    if (!edit)
        return;
    int len = GetWindowTextLengthW(edit);
    SendMessageW(edit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(edit, EM_REPLACESEL, FALSE, (LPARAM)text);
    SendMessageW(edit, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
    SendMessageW(edit, EM_SCROLLCARET, 0, 0);
}

// Everything the worker has posted so far, dealt with now. Called before a
// question goes on the screen so that the lines explaining it are already
// visible: a sent message overtakes posted ones, and a dialog that appears
// before its own explanation is a dialog that gets answered wrongly.
static void drainPosted(HWND frame)
{
    MSG m;
    while (PeekMessageW(&m, frame, WM_BCD_LINE, WM_BCD_STEP, PM_REMOVE))
        DispatchMessageW(&m);
    UpdateWindow(g_page);
    UpdateWindow(frame);
}

void beginCapture()
{
    setLineSink(sinkLine);
    setAskHook(askViaWindow);
}

static void startWork()
{
    if (g_working || g_finished || g_wiz->startBlockedNote)
        return;
    g_working = true;
    setPage(kPgWork);
    SendMessageW(g_bar, PBM_SETRANGE32, 0, (LPARAM)(g_wiz->stepCount > 0 ?
                                                    g_wiz->stepCount : 1));
    SendMessageW(g_bar, PBM_SETPOS, 0, 0);
    UpdateWindow(g_page);

    DWORD tid = 0;
    g_worker = CreateThread(0, 0, workerProc, 0, 0, &tid);
    if (!g_worker) {
        // Nothing has been touched yet: work() has not been entered. Say so and
        // stop, rather than falling back to running it on this thread.
        sayFail(L"could not start the worker thread (%s) - nothing has been changed",
                winErrText(GetLastError()));
        g_working  = false;
        g_finished = true;
        g_exitCode = g_wiz->cancelExitCode;
        g_outcome  = kOutcomeFailed;
        setPage(kPgDone);
    }
}

// ---------------------------------------------------------------------------
// The page window
// ---------------------------------------------------------------------------
static void scrollTo(HWND wnd, int pos)
{
    RECT p;
    GetClientRect(wnd, &p);
    int maxY = g_contentH - p.bottom;
    if (maxY < 0)
        maxY = 0;
    if (pos > maxY)
        pos = maxY;
    if (pos < 0)
        pos = 0;
    if (pos == g_scrollY)
        return;
    int delta = g_scrollY - pos;
    g_scrollY = pos;
    SetScrollPos(wnd, SB_VERT, g_scrollY, TRUE);
    ScrollWindowEx(wnd, 0, delta, 0, 0, 0, 0, SW_INVALIDATE | SW_ERASE);
    UpdateWindow(wnd);
}

static LRESULT CALLBACK pageProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;   // WM_PAINT fills every pixel

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        RECT c;
        GetClientRect(wnd, &c);
        // Double buffered. A page that is painted in pieces flickers, and a
        // flickering installer window is the look we are here to remove.
        HDC     mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, c.right, c.bottom);
        HGDIOBJ ob  = SelectObject(mem, bmp);
        fillRect(mem, 0, 0, c.right, c.bottom, CLR_PAGE_BG);
        SetBkMode(mem, TRANSPARENT);
        int save = SaveDC(mem);
        SetViewportOrgEx(mem, 0, -g_scrollY, 0);
        switch (g_pageIndex) {
        case kPgWelcome: renderWelcome(mem, c.right, c.bottom, false); break;
        case kPgReview:  renderReview(mem, c.right, c.bottom, false);  break;
        case kPgWork:    renderWorkChrome(mem, c.right, false);        break;
        case kPgDone:    renderDoneChrome(mem, c.right, false);        break;
        }
        RestoreDC(mem, save);
        BitBlt(dc, 0, 0, c.right, c.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(wnd, &ps);
        return 0;
    }

    case WM_VSCROLL: {
        RECT p;
        GetClientRect(wnd, &p);
        int pos = g_scrollY;
        switch (LOWORD(wp)) {
        case SB_LINEUP:   pos -= S(24); break;
        case SB_LINEDOWN: pos += S(24); break;
        case SB_PAGEUP:   pos -= p.bottom; break;
        case SB_PAGEDOWN: pos += p.bottom; break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK: pos = HIWORD(wp); break;
        }
        scrollTo(wnd, pos);
        return 0;
    }

    case WM_MOUSEWHEEL:
        scrollTo(wnd, g_scrollY - (GET_WHEEL_DELTA_WPARAM(wp) * S(40)) / WHEEL_DELTA);
        return 0;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
        SetBkColor((HDC)wp, CLR_PAGE_BG);
        SetTextColor((HDC)wp, CLR_TEXT);
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// The frame
// ---------------------------------------------------------------------------
// Everything the frame paints, into any device context.
//
// SPLIT OUT FROM paintFrame() SO THAT IT CAN BE MEASURED. The band is painted area
// OUTSIDE the page, and the pixel proof in installer/verify used to measure only
// the page - which is how a clipped glyph survived "no ink below the reported
// height, 8 of 8" and was found by an eye instead. A renderer that takes a DC can
// be asked to draw into a bitmap; one that owns its BeginPaint cannot.
static void paintFrameInto(HDC dc, int cw, int ch)
{
    int footH = S(kFootH);
    int m     = S(kMargin);

    SetBkMode(dc, TRANSPARENT);
    HeadBand hb = headBand(dc, cw);
    fillRect(dc, 0, 0, cw, hb.height, CLR_HEAD_BG);
    fillRect(dc, 0, hb.height - hb.accentH, cw, hb.accentH, CLR_ACCENT);

    RECT t = hb.title;
    SelectObject(dc, g_fHead);
    SetTextColor(dc, CLR_HEAD_TXT);
    DrawTextW(dc, g_wiz->headline, -1, &t, DT_SINGLELINE | DT_NOPREFIX |
              DT_END_ELLIPSIS);
    RECT s = hb.sub;
    SelectObject(dc, g_fHeadSmall);
    SetTextColor(dc, CLR_HEAD_DIM);
    DrawTextW(dc, g_wiz->subhead, -1, &s, DT_SINGLELINE | DT_NOPREFIX |
              DT_END_ELLIPSIS);

    int footY = ch - footH;
    fillRect(dc, 0, footY, cw, footH, CLR_FOOT_BG);
    fillRect(dc, 0, footY, cw, 1, CLR_LINE);

    // The note beside the buttons. On the work page this is the sentence that
    // makes the greyed Cancel honest.
    const wchar_t* note = 0;
    if (g_pageIndex == kPgWork && g_working)
        note = g_wiz->cannotCancelNote;
    else if (g_pageIndex == kPgReview && g_wiz->startBlockedNote)
        note = g_wiz->startBlockedNote;
    if (note) {
        RECT n = { m, footY, cw - m - 2 * S(kBtnW) - S(40), ch };
        SelectObject(dc, g_fSmall);
        SetTextColor(dc, CLR_DIM);
        DrawTextW(dc, note, -1, &n, DT_SINGLELINE | DT_NOPREFIX | DT_VCENTER |
                  DT_END_ELLIPSIS);
    }
}

static void paintFrame(HWND wnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(wnd, &ps);
    RECT c;
    GetClientRect(wnd, &c);
    paintFrameInto(dc, c.right, c.bottom);
    EndPaint(wnd, &ps);
}

static void onDpiChanged(HWND wnd, UINT dpi, const RECT* suggested)
{
    g_dpi = (int)dpi;
    buildFonts();
    applyFonts();
    if (g_wiz->showDevicePhoto)
        decodePhoto(S(kPhotoW));
    if (suggested)
        SetWindowPos(wnd, 0, suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    layout();
    InvalidateRect(wnd, 0, TRUE);
    InvalidateRect(g_page, 0, TRUE);
}

static LRESULT CALLBACK frameProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT:
        paintFrame(wnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_SIZE:
        layout();
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lp;
        mm->ptMinTrackSize.x = S(560);
        mm->ptMinTrackSize.y = S(420);
        return 0;
    }

    case WM_DPICHANGED:
        onDpiChanged(wnd, HIWORD(wp), (const RECT*)lp);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_PRIMARY:
            if (g_finished) {
                DestroyWindow(wnd);
            } else if (g_pageIndex == kPgWelcome) {
                setPage(kPgReview);
            } else if (g_pageIndex == kPgReview) {
                startWork();
            }
            return 0;
        case IDC_SECONDARY:
            if (g_pageIndex == kPgReview && g_wiz->hasWelcome)
                setPage(kPgWelcome);
            else
                SendMessageW(wnd, WM_CLOSE, 0, 0);
            return 0;
        case IDCANCEL:
            SendMessageW(wnd, WM_CLOSE, 0, 0);
            return 0;
        }
        return 0;

    case WM_BCD_LINE: {
        wchar_t* text = (wchar_t*)lp;
        if (text) {
            appendTo(g_log, text);
            if (wp & kSummaryBit)
                appendTo(g_summary, text);
            HeapFree(GetProcessHeap(), 0, text);
        }
        return 0;
    }

    case WM_BCD_STEP: {
        StepMsg* m = (StepMsg*)lp;
        int      i = (int)wp;
        if (m) {
            if (i >= 0 && i < g_wiz->stepCount && i < kMaxRows) {
                g_wiz->steps[i].state = m->state;
                if (m->detail[0]) {
                    wcsncpy(g_wiz->steps[i].detail, m->detail, kRowText - 1);
                    g_wiz->steps[i].detail[kRowText - 1] = 0;
                }
                int done = 0;
                for (int k = 0; k < g_wiz->stepCount; k++)
                    if (g_wiz->steps[k].state == kRowOk ||
                        g_wiz->steps[k].state == kRowWarn ||
                        g_wiz->steps[k].state == kRowFail ||
                        g_wiz->steps[k].state == kRowSkipped)
                        done++;
                SendMessageW(g_bar, PBM_SETPOS, (WPARAM)done, 0);
            }
            HeapFree(GetProcessHeap(), 0, m);
            layout();
            InvalidateRect(g_page, 0, FALSE);
        }
        return 0;
    }

    case WM_BCD_ASK: {
        drainPosted(wnd);
        // MB_DEFBUTTON2 so that the default is No, which is what the console's
        // "[y/N]" prompt defaults to. The two modes must not disagree about what
        // happens when somebody just presses Enter.
        int r = MessageBoxW(wnd, (const wchar_t*)lp, g_wiz->windowTitle,
                            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 |
                            MB_SETFOREGROUND);
        return (r == IDYES) ? 1 : 0;
    }

    case WM_BCD_DONE: {
        drainPosted(wnd);
        g_exitCode = (int)wp;
        g_outcome  = (Outcome)lp;
        g_working  = false;
        g_finished = true;
        if (g_worker) {
            WaitForSingleObject(g_worker, 10000);
            CloseHandle(g_worker);
            g_worker = 0;
        }
        // A run that stopped before it reached its summary has nothing captured.
        // The log is the next best thing and is better than an empty page.
        if (GetWindowTextLengthW(g_summary) == 0) {
            int len = GetWindowTextLengthW(g_log);
            if (len > 0) {
                wchar_t* buf = (wchar_t*)HeapAlloc(GetProcessHeap(), 0,
                                                   (SIZE_T)(len + 2) * sizeof(wchar_t));
                if (buf) {
                    GetWindowTextW(g_log, buf, len + 1);
                    SetWindowTextW(g_summary, buf);
                    HeapFree(GetProcessHeap(), 0, buf);
                }
            }
        }
        // THE LAST PAGE IS A DOCUMENT, NOT A LIVE TAIL. appendTo() has been
        // scrolling the caret to the end of the pane with every line, which is
        // right while the run is in flight and wrong the moment it stops: without
        // this the page opens showing the BOTTOM of the summary, so the verdict and
        // the numbered list of things still to do are off screen at the exact
        // moment somebody looks at them.
        SendMessageW(g_summary, EM_SETSEL, 0, 0);
        SendMessageW(g_summary, EM_SCROLLCARET, 0, 0);
        SendMessageW(g_bar, PBM_SETPOS, (WPARAM)g_wiz->stepCount, 0);
        setPage(kPgDone);
        return 0;
    }

    case WM_CLOSE:
        if (g_working) {
            // THE ONE PROMISE THIS WINDOW MUST NOT MAKE. The steps write files
            // and registry keys; there is no point in the middle of them where
            // stopping leaves the machine in a state anybody chose. So the answer
            // is no, with the reason, rather than a cancel that half undoes an
            // install.
            MessageBoxW(wnd,
                        L"This cannot be stopped now.\n\n"
                        L"The driver file and the registry entry are being written. "
                        L"Stopping half way through would leave this machine in a "
                        L"state nobody chose, so the only safe thing is to let it "
                        L"finish - it takes a few seconds.\n\n"
                        L"When it is done you can undo all of it with "
                        L"BCD3000Uninstall.exe.",
                        g_wiz->windowTitle, MB_OK | MB_ICONWARNING);
            return 0;
        }
        DestroyWindow(wnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Setting up
// ---------------------------------------------------------------------------
static bool g_classesDone = false;

bool init()
{
    g_inst = GetModuleHandleW(0);

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS;
    if (!InitCommonControlsEx(&icc))
        return false;

    // The window needs COM for WIC, and the driver's registration later needs it
    // too - on the worker thread, where common.cpp initialises its own. An
    // apartment here, an apartment there, no sharing between them.
    CoInitializeEx(0, COINIT_APARTMENTTHREADED);

    NONCLIENTMETRICSW ncm;
    ZeroMemory(&ncm, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    wcscpy(g_faceName, L"Segoe UI");
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0) &&
        ncm.lfMessageFont.lfFaceName[0]) {
        wcsncpy(g_faceName, ncm.lfMessageFont.lfFaceName, LF_FACESIZE - 1);
        g_faceName[LF_FACESIZE - 1] = 0;
    }

    if (!g_classesDone) {
        WNDCLASSEXW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = frameProc;
        wc.hInstance     = g_inst;
        wc.hCursor       = LoadCursorW(0, BCD_CURSOR_ARROW);
        wc.lpszClassName = L"BcdWizardFrame";
        wc.hIcon         = LoadIconW(g_inst, MAKEINTRESOURCEW(IDI_APPICON));
        wc.hIconSm       = wc.hIcon;
        if (!RegisterClassExW(&wc))
            return false;

        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = pageProc;
        wc.hInstance     = g_inst;
        wc.hCursor       = LoadCursorW(0, BCD_CURSOR_ARROW);
        wc.lpszClassName = L"BcdWizardPage";
        if (!RegisterClassExW(&wc))
            return false;
        g_classesDone = true;
    }
    return true;
}

void detachConsole()
{
    // A console subsystem program is what keeps /console honest: a shell waits
    // for one and can redirect its output, and that is how an automated run reads
    // the installer's own words back. The price is that Windows has already
    // created a console window by the time this code runs, so the window mode
    // hides it and then lets it go.
    HWND con = GetConsoleWindow();
    if (con)
        ShowWindow(con, SW_HIDE);
    setConsoleEcho(false);
    DWORD pids[4] = { 0, 0, 0, 0 };
    if (GetConsoleProcessList(pids, 4) == 1)
        FreeConsole();   // ours alone, so it can go entirely
}

static UINT dpiForWindow(HWND wnd)
{
    typedef UINT (WINAPI *Fn)(HWND);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    Fn      f = u ? (Fn)GetProcAddress(u, "GetDpiForWindow") : 0;
    if (f) {
        UINT d = f(wnd);
        if (d >= 72)
            return d;
    }
    HDC dc = GetDC(0);
    UINT d = dc ? (UINT)GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc)
        ReleaseDC(0, dc);
    return d < 72 ? 96 : d;
}

int runWizard(Wizard* wiz)
{
    g_wiz      = wiz;
    g_exitCode = wiz->cancelExitCode;
    g_outcome  = kOutcomeStopped;
    g_pageIndex = wiz->hasWelcome ? kPgWelcome : kPgReview;

    // Created hidden and at a nominal size, then measured, sized and centred:
    // the DPI is not known until there is a window to ask about, and creating it
    // at the wrong size first would show the wrong size first.
    HWND frame = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_APPWINDOW,
                                 L"BcdWizardFrame", wiz->windowTitle,
                                 WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                 WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 100, 100,
                                 0, 0, g_inst, 0);
    if (!frame)
        return wiz->cancelExitCode;
    g_frame = frame;
    g_dpi   = (int)dpiForWindow(frame);
    buildFonts();

    g_page = CreateWindowExW(WS_EX_CONTROLPARENT, L"BcdWizardPage", L"",
                             WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_VSCROLL,
                             0, 0, 10, 10, frame, 0, g_inst, 0);
    g_primary = CreateWindowExW(0, L"BUTTON", L"",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                BS_DEFPUSHBUTTON,
                                0, 0, 10, 10, frame, (HMENU)IDC_PRIMARY, g_inst, 0);
    g_secondary = CreateWindowExW(0, L"BUTTON", L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                  0, 0, 10, 10, frame, (HMENU)IDC_SECONDARY,
                                  g_inst, 0);
    g_log = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                            WS_CHILD | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE |
                            ES_READONLY | ES_AUTOVSCROLL,
                            0, 0, 10, 10, g_page, (HMENU)IDC_LOG, g_inst, 0);
    g_summary = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE |
                                ES_READONLY | ES_AUTOVSCROLL,
                                0, 0, 10, 10, g_page, (HMENU)IDC_SUMMARY, g_inst, 0);
    g_bar = CreateWindowExW(0, PROGRESS_CLASSW, L"",
                            WS_CHILD | PBS_SMOOTH,
                            0, 0, 10, 10, g_page, (HMENU)IDC_BAR, g_inst, 0);
    if (!g_page || !g_primary || !g_secondary || !g_log || !g_summary) {
        DestroyWindow(frame);
        g_frame = 0;
        releaseEarly();
        return wiz->cancelExitCode;
    }
    SendMessageW(g_log, EM_SETLIMITTEXT, 0, 0);
    SendMessageW(g_summary, EM_SETLIMITTEXT, 0, 0);
    applyFonts();

    // Everything that was said before there was anywhere to show it: the banner,
    // what this installer is carrying, and the whole machine report the checks
    // page was built from. It goes in first so that the log pane is the complete
    // run and not just the part after the button was pressed.
    if (g_early && g_early[0]) {
        SetWindowTextW(g_log, g_early);
        SendMessageW(g_log, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
        SendMessageW(g_log, EM_SCROLLCARET, 0, 0);
    }
    releaseEarly();

    if (wiz->showDevicePhoto && !decodePhoto(S(kPhotoW))) {
        // Not a reason to refuse to install. The page simply has no picture and
        // the words move left.
        wiz->showDevicePhoto = false;
    }

    RECT want = { 0, 0, S(kWinW), S(kWinH) };
    AdjustWindowRectEx(&want, (DWORD)GetWindowLongPtrW(frame, GWL_STYLE), FALSE,
                       (DWORD)GetWindowLongPtrW(frame, GWL_EXSTYLE));
    int ww = want.right - want.left;
    int wh = want.bottom - want.top;
    int sx = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    SetWindowPos(frame, 0, sx, sy, ww, wh, SWP_NOZORDER);

    setPage(g_pageIndex);
    ShowWindow(frame, SW_SHOW);
    UpdateWindow(frame);
    SetForegroundWindow(frame);

    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0) > 0) {
        if (!IsDialogMessageW(frame, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    // If the loop ended while the work was still running, wait for it. That takes
    // an outside force - WM_CLOSE refuses while g_working - but if it happens,
    // returning here would let the process exit in the middle of a registry write,
    // which is the one outcome worth blocking a few seconds for. Bounded, not
    // infinite: a worker that has somehow wedged must not wedge the exit as well.
    if (g_worker) {
        WaitForSingleObject(g_worker, 30000);
        CloseHandle(g_worker);
        g_worker = 0;
    }

    // The sink and the hook are cleared before the window handles go, so that a
    // late say*() from anywhere cannot post to a window that is gone.
    setLineSink(0);
    setAskHook(0);
    g_frame = 0;
    g_page  = 0;
    releasePhoto();
    releaseFonts();
    releaseEarly();
    return g_exitCode;
}

// ---------------------------------------------------------------------------
// The pre-flight page, from a MachineState that has already been gathered.
// Reads nothing, decides nothing: every judgement below is the same judgement
// reportMachineState() prints, said in fewer words.
// ---------------------------------------------------------------------------
void fillPreflightRows(Wizard* wiz, const MachineState* s)
{
    int n = 0;

    // 1. The ASIO registration.
    if (!s->asio.clsidKeyPresent && !s->asio.asioNameKeyPresent) {
        setRow(&wiz->review[n++], kRowNeutral, L"ASIO driver registration",
               L"Nothing is registered on this machine yet. This is what the "
               L"installer is for.");
    } else if (s->registeredElsewhere) {
        setRow(&wiz->review[n++], kRowWarn, L"ASIO driver registration",
               L"A copy is already registered, from somewhere else:\n%s\n"
               L"There is one registration per machine, so installing points every "
               L"host application at the install folder instead. You will be asked "
               L"before that happens, and the old path is recorded so the "
               L"uninstaller can put it back.",
               s->asio.inprocPath);
    } else {
        setRow(&wiz->review[n++], kRowOk, L"ASIO driver registration",
               L"Already registered from the install folder:\n%s",
               s->asio.inprocPath);
    }

    // 2. The control and LED service.
    {
        bool installed = s->pathsResolved && fileExists(s->bridgeTarget);
        if (s->bridge.running)
            setRow(&wiz->review[n++], kRowOk, L"Control and LED service",
                   L"Running (%d process%s). It will be left alone unless its file "
                   L"differs from the one in this installer, and even then it is not "
                   L"replaced without /replace-service: stopping it destroys the "
                   L"virtual MIDI port.",
                   s->bridge.instanceCount,
                   s->bridge.instanceCount == 1 ? L"" : L"es");
        else if (installed)
            setRow(&wiz->review[n++], kRowWarn, L"Control and LED service",
                   L"Installed but not running. The knobs, buttons and LEDs are dead "
                   L"until it starts; it starts by itself at your next sign in.");
        else
            setRow(&wiz->review[n++], kRowNeutral, L"Control and LED service",
                   L"Not installed yet. It goes into your own profile and starts at "
                   L"sign in from a Startup shortcut, unelevated on purpose.");
    }

    // 3. teVirtualMIDI. Third party; detected, never installed by us.
    switch (s->tevm.state) {
    case kTeVmPresent:
        setRow(&wiz->review[n++], kRowOk, L"teVirtualMIDI (third party)",
               L"Found at %s.", s->tevm.hardCodedPath);
        break;
    case kTeVmNotWhereTheServiceLooks:
        setRow(&wiz->review[n++], kRowFail, L"teVirtualMIDI (third party)",
               L"Found at %s but not at %s, which is the literal path the control "
               L"service opens. The service will fail to start.",
               s->tevm.systemDirPath, s->tevm.hardCodedPath);
        break;
    default:
        setRow(&wiz->review[n++], kRowFail, L"teVirtualMIDI (third party)",
               L"Missing. Audio will work without it; the knobs, buttons and LEDs "
               L"will not. It comes with loopMIDI. This installer does not install "
               L"it - it is not ours to redistribute.");
        break;
    }

    // 4. The WinUSB binding on the device.
    if (!s->usb.enumKeyPresent)
        setRow(&wiz->review[n++], kRowFail, L"WinUSB binding on the device",
               L"The BCD3000 has never been seen by this machine, or it is not bound "
               L"to WinUSB. Nothing works before this: it is how the driver reaches "
               L"the hardware. It is done once, by hand, with Zadig - this installer "
               L"deliberately will not rebind a USB device.");
    else if (!s->usb.guidPresent)
        setRow(&wiz->review[n++], kRowFail, L"WinUSB binding on the device",
               L"The device is known to this machine but the MI_00 function has no "
               L"WinUSB interface, so the binding is missing. Run Zadig once.");
    else if (!s->usb.interfacePresentNow)
        setRow(&wiz->review[n++], kRowWarn, L"WinUSB binding on the device",
               L"Bound (%s), but the device is not connected right now, so nothing "
               L"about the hardware itself could be confirmed.", s->usb.guid);
    else
        setRow(&wiz->review[n++], kRowOk, L"WinUSB binding on the device",
               L"Bound and connected right now (%s).", s->usb.guid);

    // Not a check, but the one thing on this page that changes what the installer
    // is allowed to do, so it belongs where the checks are and not in a log.
    if (!s->elevated)
        setRow(&wiz->review[n++], kRowFail, L"Administrator rights",
               L"This process is not elevated. Registering an ASIO driver writes to "
               L"HKEY_CLASSES_ROOT and to HKLM\\SOFTWARE\\ASIO, and the driver goes "
               L"into %%ProgramFiles%%. Nothing will be installed.");
    else if (s->account.checked && !s->account.matched)
        setRow(&wiz->review[n++], kRowWarn, L"Which account this is for",
               L"Running as %s while the desktop belongs to %s. The driver is machine "
               L"wide and will be installed; the control service and its startup "
               L"shortcut live in a user profile and will be refused, because they "
               L"would go into the wrong one.",
               s->account.tokenAccount, s->account.shellAccount);
    else if (!s->account.checked)
        setRow(&wiz->review[n++], kRowWarn, L"Which account this is for",
               L"The account that owns the desktop could not be read, so the per user "
               L"folders are this process's own profile.");

    wiz->reviewCount = n;
}

}
