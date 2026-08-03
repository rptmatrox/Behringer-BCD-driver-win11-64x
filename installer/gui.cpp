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
#include <dbt.h>
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

// The words on the re-check button. ONE definition: the control is created with it
// and setup.cpp's rows point at it by name. See the block in gui.h.
const wchar_t* const kRecheckLabel = L"Check again";

// ---------------------------------------------------------------------------
// Messages from the worker thread. The worker posts; the window's own thread is
// the only one that ever touches a control.
// ---------------------------------------------------------------------------
#define WM_BCD_LINE  (WM_APP + 1)  // wParam: kind + kSummaryBit. lParam: heap text
#define WM_BCD_STEP  (WM_APP + 2)  // wParam: row index. lParam: heap StepMsg
#define WM_BCD_DONE  (WM_APP + 3)  // wParam: exit code. lParam: Outcome
#define WM_BCD_ASK   (WM_APP + 4)  // SENT, not posted. lParam: the question
#define WM_BCD_ROW   (WM_APP + 5)  // wParam: review row index. lParam: heap RowMsg
#define WM_BCD_RDONE (WM_APP + 6)  // wParam: the review's new row count
// WM_APP + 7 was WM_BCD_OFFER, which carried the action button's new words from the
// re-check worker. The offer is a column of the screen table now and refreshScreens()
// rebuilds it from the same fresh MachineState on the same handler, so the message
// was a second channel for one fact. The number is left unused rather than reused:
// nothing posts it, and a value that changed meaning between builds is the kind of
// thing that goes wrong once and then cannot be reasoned about.

// WM_BCD_LINE and WM_BCD_STEP have to stay adjacent and in this order: they are
// drained as one range before a question is put on the screen, so that the lines
// that explain the question are already visible behind it.
//
// WM_BCD_ROW and WM_BCD_RDONE are deliberately OUTSIDE that range. They belong to
// the re-check, which asks nothing and runs when no install is in flight, so there
// is no question for them to have to arrive in front of.

const WPARAM kSummaryBit = 0x100;

// This project does not define UNICODE, so IDC_ARROW expands to the ANSI flavour
// of MAKEINTRESOURCE and cannot be handed to LoadCursorW. 32512 is IDC_ARROW.
#define BCD_CURSOR_ARROW MAKEINTRESOURCEW(32512)
// ...and 32649 is IDC_HAND, for the same reason and with the same caveat. It is the
// pointer over the painted download address, which is the only region in this program
// that acts without being a control. See pageProc()'s WM_SETCURSOR.
#define BCD_CURSOR_HAND  MAKEINTRESOURCEW(32649)

#define IDC_PRIMARY   1001
#define IDC_SECONDARY 1002
#define IDC_LOG       1003
#define IDC_SUMMARY   1004
#define IDC_BAR       1005
#define IDC_RECHECK   1006
#define IDC_REVIEWPANE 1007
#define IDC_ACTION    1008
#define IDC_OVERRIDE  1009
// The two choice controls. CONSECUTIVE AND IN THE TABLE'S ORDER, because the handler
// turns the command id back into the index it selects by subtracting the first - one
// arithmetic step instead of a two arm branch that could disagree with the labels.
#define IDC_MODEL0    1010
#define IDC_MODEL1    1011

// THERE IS NO PAGE ENUM ANY MORE. What used to be
//   enum { kPgWelcome = 0, kPgReview = 1, kPgWork = 2, kPgDone = 3 };
// is now an index into Wizard::screens, and which KIND of screen it is is asked of
// the entry rather than of the number. The enum was not merely a spelling: it made
// "which page is this" a question with four hard coded answers, so every new subject
// had to be squeezed onto one of the four - which is precisely how one page came to
// carry six rounds of unrelated work.

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
// metrics of the two fonts that draw the band. See the comment there - including what
// the floor is FOR, which is the degenerate case and not the ordinary one.
const int kHeadH     = 70;
const int kHeadPadTop    = 12;   // above the headline
const int kHeadGap       = 2;    // between the headline and the subhead
const int kHeadPadBottom = 8;    // between the subhead and the rule that closes it
// *** THE FOOT BAND GREW, AND THE NUMBER WAS MEASURED RATHER THAN CHOSEN. ***
//
// It was 60 for as long as the band held one button and a sentence with 266 logical
// pixels to wrap in. Page 2 now has TWO buttons on the left, so the sentence gets
// what they leave - about 106 at 96 DPI - and a note that used to be one line is
// three. Three lines of the small font are 39 of the old box's 48 at 96 DPI and 69
// of its 72 at 144, which is 96 per cent of the room for text that is about to be
// read by somebody whose install has just been refused. That is not a margin, it is
// a cliff with a check standing at the edge of it.
//
// 72 gives 60 and 90, so three lines sit at 65 and 77 per cent and a fourth line
// still fits at 96 DPI. The cost is 12 logical pixels of page height on a page that
// already scrolls. installer/verify measures EVERY note this program can put there
// against the box the WIDEST button label leaves, so the next longer sentence fails
// a check instead of losing its second half.
const int kFootH     = 72;
const int kMargin    = 26;
const int kBtnW      = 122;
const int kBtnH      = 32;
// Page 2's re-check button. Narrower than the two on the right on purpose: it is
// not the choice this page is about, and the room it does not take is room the note
// beside it keeps.
const int kRecheckW  = 104;
// Page 2's contextual action button, immediately to the right of "Check again" in
// the same fixed band. See the block on Wizard::actionLabel in gui.h for why it is
// there and not on the row it is about.
//
// ITS WIDTH IS WORKED OUT FROM ITS WORDS, not fixed, because the words change with
// what is being offered - and a button whose label is cut is exactly the defect
// this project keeps finding. kActionMaxW is the ceiling, and it is not decoration:
// the note beside it lives on whatever the two buttons leave, so a button allowed
// to grow without limit is a button that eats a sentence. installer/verify measures
// both halves - that every label this button can carry fits INSIDE it, and that the
// note still fits in what is left.
const int kActionGap  = 10;   // between "Check again" and it
const int kActionPadW = 26;   // total horizontal padding around the label
const int kActionMinW = 90;
const int kActionMaxW = 168;
// *** THE NAMED DOOR IS ALLOWED TO BE WIDER THAN THE ACTION BUTTON, AND THE NUMBER
//     WAS MEASURED RATHER THAN CHOSEN. *** kActionMaxW is a ceiling that exists to
// stop a contextual offer eating the note beside it. The door carries a SENTENCE and
// not a verb - it has to say what taking it claims, or it is a second Next with more
// words - and at 96 DPI "The binding is already applied - continue anyway" needs more
// than 168 logical pixels.
//
// WHAT PAYS FOR IT: the screen the door stands on has no foot note at all. footNote()
// paints startBlockedNote only where the press STARTS the work, which is a different
// screen, so the strip between "Check again" and Back is empty on this one. The two
// halves are measured by installer/verify: that the label fits INSIDE the button at
// both DPIs, and that the note - on the screens that have one - still fits in what
// the buttons leave.
const int kOverrideMaxW = 260;
const int kPhotoW    = 210;
// The Zadig screenshot on page 2, at its NATIVE width. docs\Zadig.png is 574 x 254,
// so at 96 DPI it is drawn one pixel per pixel and the two strings the user has to
// match are as legible on the page as they were on the screen it was taken from.
//
// IT FITS, AND THAT IS ARITHMETIC RATHER THAN LUCK: the window is kWinW wide, the
// page loses the frame and the scroll bar, and page 2 measures 683 pixels at 96 DPI.
// 574 plus two kMargin of 26 is 626. The window has no WS_THICKFRAME, so that number
// cannot be made smaller by a user - but renderReview() still checks before it draws,
// because a page too narrow for the picture must lose the picture, not the words
// beside it.
const int kZadigShotW = 574;
// The border of page background left around a drawn picture by drawPicture(): the
// plate it fills is kPicturePad larger than the image on every side, and the image
// is blended that far inside it. ONE definition, because renderSubject() records the
// rectangle AlphaBlend really writes and had to know where inside the plate that is -
// and a second copy of this number is precisely how the recorded box came to describe
// something nothing draws. See lastZadigShotBox() in gui.h.
const int kPicturePad = 8;
const int kMarkSize  = 15;

// ---------------------------------------------------------------------------
// THE TWO CHOICE CONTROLS, AND WHY THEIR HEIGHT IS MEASURED RATHER THAN FIXED.
//
// They are the only controls in this program that sit ON THE PAGE. Everything else
// that can be pressed is in the foot band, because the page scrolls and scrollTo()
// deliberately does not pass SW_SCROLLCHILDREN - a control on a scrolling page stays
// still while the painted rows slide under it, which is association by position at
// its worst.
//
// *** WHAT MAKES THE PAGE SAFE HERE IS A CHECK AND NOT A HOPE. *** The screen these
// two stand on is held at deficit ZERO by Rule 1's ratchet in installer\verify: a
// screen not named in allowedDeficit() is allowed no overflow at all, so the day this
// screen's content grows past its strip the suite goes red BEFORE the page can raise
// a scroll bar under the controls. The foot band's argument is "this strip never
// moves"; this screen's argument is "this page never scrolls, and that is asserted".
// They are different arguments and only one of them needed a new check.
//
// AND THEY ARE ON THE PAGE AND NOT IN THE BAND FOR A REASON OF THEIR OWN: a radio
// button is not an action, it is the subject of the screen. The foot band is
// navigation plus at most one contextual offer, and two radios wedged in beside Back
// and Next would be the only place in this program where the thing a screen is ABOUT
// lives outside the page.
//
// THE HEIGHT COMES FROM THE WORDS, exactly like the action button's width. A fixed
// row height is a promise about how long a label is, and the label that says what the
// BCD2000 does not support is the longest string either control carries. Each option
// is measured WRAPPED, in the width the page really gives it, so a longer sentence at
// a higher DPI moves the second option down instead of being cut.
// *** kChoicePadH WAS 6 AND IS 3, AND THE NUMBER CAME OUT OF A MEASUREMENT THAT ALSO
//     CORRECTED WHICH CONSTANT WAS AT FAULT. ***
//
// A review found this screen reserving 68 logical pixels per option at 144 DPI for labels
// comctl32 says need 52 - 32 pixels of slack across the pair, against a screen with only
// 23 pixels of headroom over its strip. It named kChoiceIndW as the cause, on the reasoning
// that too generous an indicator allowance measures the label too narrow and buys an extra
// wrapped line.
//
// MEASURED, and it is not that. BCM_GETIDEALSIZE's answer came back as this function's own
// text height PLUS EXACTLY 2, at both DPIs and for both labels - 34 vs 36 at 96, 50 vs 52
// at 144. An extra wrapped line would be 17 or 25 pixels, not 2, so the LINE COUNT
// kChoiceIndW produces is right and the 2 is comctl32's own vertical padding. The slack
// per option is therefore 2 x S(kChoicePadH) - 2, which at the old value of 6 is 10 at 96
// DPI and 16 at 144 - the 20 and 32 across the pair that the review measured, arrived at
// from the other constant.
//
// So kChoiceIndW stays 24 - it is guarded downward by the anti-clipping check, which is the
// direction that costs a sentence - and the padding, which is the direction that only ever
// spends strip, comes down to 3. That leaves 4 pixels of air at 96 DPI and 8 at 144 on top
// of comctl32's own, and installer\verify now bounds the slack from ABOVE as well, so
// neither direction is a hope. See allowedChoiceSlack() there, which also records that the
// 8 was predicted as 6 until MulDiv's rounding was read properly.
const int kChoiceLeadH = 10;  // between the row above and the first option
const int kChoiceGap   = 6;   // between the two options
const int kChoiceIndW  = 24;  // the round indicator BS_AUTORADIOBUTTON draws itself
const int kChoicePadH  = 3;   // above and below a label inside its own control

// ---------------------------------------------------------------------------
// *** THE PAPER UNDER THE LAST LINE OF A PAGE. IT IS SPACE, NOT CONTENT, AND EVERY
//     RENDERER HERE USED TO REPORT IT AS CONTENT. ***
//
// The number a render*() returns is asked two different questions: "how far may this
// page be scrolled" and "does this screen FIT". A trailing bottom margin is the right
// answer to the first and the wrong answer to the second, and the difference was not
// academic - it is what put a second scrolling surface on the one screen in this
// program that already had a pane.
//
// MEASURED, at 144 DPI, on the MIDI port screen: the ink ended at row 305 of a 330
// pixel strip and the renderer reported 349. So the page raised a scroll bar, on a
// screen whose pane scrolls too, to reveal 44 rows of blank paper - a scroll inside a
// scroll, which is the exact complaint this redesign opened with, bought for nothing
// anybody can read. The opening page's 12 pixel overflow at 144 DPI was the same
// margin and no capture had ever shown either, because a capture is rendered at the
// height the renderer asks for.
//
// So the renderers report what has to be VISIBLE, and layout() adds this to the
// scroll RANGE when - and only when - the page is scrolling anyway, so that a reader
// who scrolls to the end still gets paper under the last line instead of a sentence
// jammed against the foot band. Two questions, two numbers, and neither is derived
// from the other.
const int kPageBottomPad = 20;

// Page 2's text pane. It takes the bottom of the page and the rows scroll in what
// is left above it.
//
// *** WHY IT DOES NOT SCROLL WITH THE ROWS, WHICH IS THE SAME REASON THE "CHECK
//     AGAIN" BUTTON IS IN THE FOOT BAND. *** scrollTo() deliberately does not pass
// SW_SCROLLCHILDREN, so a child window placed on this page stays where it is while
// the painted rows slide under it. For a button that would be wrong in one way; for
// a pane it would be wrong in a worse one, because a pane covers the area it is in
// and the rows would be scrolling INTO something the user cannot see behind.
// Passing SW_SCROLLCHILDREN instead would drag the pane off the top of the window
// as soon as somebody reads the last row.
//
// So the page is split: the strip above the pane scrolls, the pane does not, and
// the pane has a scroll bar of its own for its own overflow. kReviewPaneShare is
// the fraction of the page it asks for; kReviewPaneMinH is the floor under it and
// kReviewRowsMinH is the floor under the rows, so that neither half can be squeezed
// to nothing on a short window.
const int kReviewPaneShare = 42;   // per cent of the page's height
const int kReviewPaneMinH  = 92;
const int kReviewRowsMinH  = 120;
const int kReviewPaneGap   = 10;

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
static HWND      g_recheckBtn  = 0;
static HWND      g_overrideBtn = 0;
static HWND      g_review      = 0;
// The two choice controls, and they are children of the PAGE and not of the frame -
// the only pressable controls in this program that are. See kChoiceLeadH for why that
// is safe on this one screen and on no other.
static HWND      g_modelBtn[2] = { 0, 0 };
// The box renderSubject() RESERVED for them, in page coordinates, and whether it
// reserved one at all. Recorded on the measuring pass as well as the painting one,
// because the measuring pass is the one installer\verify can run without a visible
// window - and a real control is the one thing on a page no capture can photograph.
// See lastModelChoiceBox() in gui.h.
static RECT      g_choiceBox   = { 0, 0, 0, 0 };
static bool      g_choiceShown = false;
// Where each option's own control goes inside that box. Written by the same pass, so
// that layout() places the real windows exactly where the measurement said they would
// be instead of computing the arithmetic a second time.
static RECT      g_choiceRow[2];
// The box renderSubject() drew the download address in, in page coordinates, and
// whether it drew one at all. Recorded on the measuring pass as well as the painting
// one, for the reason the choice's box is. See lastAddressBox() in gui.h.
static RECT      g_addrBox     = { 0, 0, 0, 0 };
static bool      g_addrShown   = false;
// ...and the part of it a click opens, which is the accent-coloured run alone and not
// the whole column. See lastAddressLinkBox() in gui.h for the three regions that were
// considered and why the other two are wrong. Zeroed with the box above, so a screen
// that says nothing about downloading has nothing clickable on it either.
static RECT      g_addrLinkBox = { 0, 0, 0, 0 };
static bool      g_addrLink    = false;
// The width the address ITSELF needed, and the width it was given. Two numbers and not
// one, because "the address is on the screen" and "the whole of the address is on one
// line" are different questions and only the second is about hunting: a wrapped or cut
// address is an address somebody retypes wrong. Recorded rather than reasoned, because
// the answer depends on a font at a DPI.
static int       g_addrNeedW   = 0;
static int       g_addrHaveW   = 0;
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

// Page 2's Zadig screenshot. Same three variables, same decoder, same lifetime rules
// as the photograph above - thrown away and decoded again when the DPI changes.
static HBITMAP   g_shot        = 0;
static int       g_shotW       = 0;
static int       g_shotH       = 0;

// Which screen the window is on: an INDEX INTO Wizard::screens, and nothing else.
// Entry 0 is where every flow opens, which is why this no longer has to ask whether
// the flow has a welcome page - a flow without one simply does not have that entry.
static int       g_screen      = 0;
static int       g_scrollY     = 0;
static int       g_contentH    = 0;
// *** HOW FAR THE PAGE MAY BE SCROLLED, WHICH IS NOT g_contentH. *** g_contentH is
// what has to be VISIBLE - the bottom of the last thing the renderer paints - and it
// is the number Rule 1 is measured against. This is that plus the bottom margin, and
// only on a page that is scrolling anyway, so that a reader who scrolls to the end
// gets paper under the last line. See kPageBottomPad for what conflating the two
// cost. Written by layout(), read by scrollTo().
static int       g_scrollH     = 0;

// How much of the page actually SCROLLS. It is the page's own height on every page
// but page 2 with a text pane, where the pane takes the bottom and only the strip
// above it moves. Kept as one number rather than recomputed at each of the four
// places that need it - the scroll info, the clamp, a wheel notch and a page down -
// because four copies of a piece of arithmetic is how a scroll bar comes to promise
// a range the page cannot reach.
static int       g_viewH       = 0;
static bool      g_working     = false;
static bool      g_finished    = false;
static int       g_exitCode    = 0;
static Outcome   g_outcome     = kOutcomeFailed;
static HANDLE    g_worker      = 0;

// The re-check. Its own handle, so that a re-check and an install can never be
// confused for one another, and its own thread id beside it - which is what lets
// installer/verify measure THE one rule of this file (see the head of gui.h)
// instead of taking it on trust: the id is CreateThread's own out parameter, and a
// re-check that had run on the window thread would report the window's id.
static HANDLE    g_recheck     = 0;
static DWORD     g_recheckTid  = 0;
static bool      g_rechecking  = false;

// The contextual action of whichever screen carries one. It shares the re-check's
// thread slot and the re-check's in flight flag ON PURPOSE, and that is a correctness
// property rather than a saving: an action ENDS by measuring again, so an action and
// a re-check running at once would be two writers of the same rows. One slot makes
// that impossible instead of forbidden. g_runningAction only says whether recheckProc()
// is about to call one of the screen's own pointers or the flow's plain re-check.
//
// (It used to say "which of the TWO pointers". There are three since the painted
// address became clickable, and the address's opener is the one that is not a button -
// so the sentence would have gone stale about the exact thing this file's comments keep
// going stale about: a list nobody re-derived.)
static HWND      g_actionBtn   = 0;
static bool      g_runningAction = false;
// WHICH action is in flight, taken from the screen at the moment of the press. The
// user can still navigate while it runs - Back is deliberately not disabled - so the
// screen the window is ON is not a safe place to read this from once the worker has
// started. See recheckProc().
static int     (*g_actionFn)(void*) = 0;
// *** AND THE THIRD THING THAT CAN BE IN THAT ONE SLOT: THE PAINTED ADDRESS WAS
//     CLICKED. *** It is a second pair of variables rather than a cast into the one
// above because its signature is different on purpose - it is handed the URL it is to
// open, so that ONE function in setup.cpp serves both screens and neither can be
// pointed at the other's page. See Screen::addressOpen in gui.h.
//
// Both are taken from the screen at the moment of the click, for the reason g_actionFn
// is: Back stays alive while a worker runs, so the screen the window is ON is not a
// safe place to read either of them from once the thread has started.
static int     (*g_addressFn)(void*, const wchar_t*) = 0;
static const wchar_t* g_addressUrl = 0;
// The hand pointer, for the one region in this program that acts without being a
// control. A painted address gives Windows nothing to change the cursor over, and a
// region that acts with no affordance at all is a region only somebody who already
// guessed will ever press. It costs no pixel, so no capture moves for it - which is
// also why installer/verify has to assert it directly; a picture cannot photograph a
// cursor. Loaded once beside the fonts and never destroyed: LoadCursorW on a system
// cursor returns a shared handle that must not be freed.
static HCURSOR   g_handCursor  = 0;
// Where the pointer was when the page last heard about it, in the page's CLIENT
// coordinates. WM_SETCURSOR is not given a position and GetCursorPos cannot be used
// from a thread whose desktop is not the input one - see pageProc()'s two mouse arms.
// It starts at (-1,-1) rather than at the origin: (0,0) is a real point on the page,
// and a program whose pointer has never moved must not claim it is sitting there.
static int       g_lastMoveX   = -1;
static int       g_lastMoveY   = -1;
// The words on it right now. A bounded copy rather than the pointer, because the
// control is one control and its text is set from whichever screen the window is on.
static wchar_t   g_actionLabel[128] = { 0 };
// ...and the door's words, for the same reason and in the same shape.
static wchar_t   g_overrideLabel[128] = { 0 };

// *** WHERE THE ZADIG PICTURE ENDED UP, RECORDED BY THE RENDERER THAT DRAWS IT. ***
// See lastZadigShotBox() in gui.h. Written on the measuring pass too, so that a
// harness which never shows the window can still ask the question; g_shotDrawn is
// false when the page was too narrow and the picture was skipped rather than
// squeezed, which is the one silent behaviour on this screen.
static RECT      g_shotBox   = { 0, 0, 0, 0 };
static bool      g_shotDrawn = false;

// Something was plugged in or unplugged while page 2 was on the screen. A FLAG, and
// nothing else: see the comment on the WM_DEVICECHANGE handler.
static bool      g_deviceChanged = false;

// The one sentence a device arrival is allowed to produce. Named here so that the
// page paints it and installer/verify measures the same string.
static const wchar_t* const kDeviceNudge =
    L"Something changed on this machine. Press Check again.";

// Written and read on the worker thread only.
static bool      g_capture     = false;
static Outcome   g_workerOutcome = kOutcomeFailed;

static int S(int logical) { return MulDiv(logical, g_dpi, 96); }

// ---------------------------------------------------------------------------
// The three pure predicates. Declared in gui.h, and see the block there for why
// they are functions at all rather than expressions inside frameProc().
//
// NOTHING IN THESE THREE TOUCHES A GLOBAL. They are handed the Wizard, so the
// harness can ask them about a table it built itself, on a machine with no window
// and no desktop - and so that they cannot come to depend on which screen happens
// to be on the screen at the time.
// ---------------------------------------------------------------------------
int screenCount(const Wizard* w)
{
    if (!w)
        return 0;
    int n = 0;
    while (n < kMaxScreens && w->screens[n].title)
        n++;
    return n;
}

const wchar_t* primaryLabelFor(const Wizard* w, int screen, bool finished)
{
    if (!w || screen < 0 || screen >= screenCount(w))
        return L"Close";
    // The finished flow always closes, whatever the table says, because the work
    // has run and there is nothing else the button could honestly offer.
    if (finished)
        return L"Close";
    return w->screens[screen].primaryLabel;
}

// WHAT THE PRIMARY BUTTON DOES ON A SCREEN. See the block in gui.h for why this is
// a value and not a branch in frameProc(); what follows is only the rule itself.
//
// *** IT IS DERIVED FROM THE KIND AND FROM NOTHING ELSE, AND THAT IS WHAT MAKES
//     "Install appears exactly once" A MEASUREMENT. *** The label is data written by
// setup.cpp; the action is this rule, written here. They come from two different
// files, so installer/verify comparing one against the other is comparing two
// sources - which is the property four checks in this project turned out not to
// have, each of them unable to fail because both of its sides read the same thing.
//
// *** A CHECK SCREEN STARTS THE WORK ONLY IF ITS ENTRY SAYS SO. *** This used to
// read `case kScreenCheck: return kPrimaryStart;`, with no other condition, and the
// comment here used to explain that the day a second check screen existed the rule
// would become wrong LOUDLY - the harness asserts that exactly one screen is
// kPrimaryStart, so the task that added it would fail that check. That was true and
// it was not enough. The design's finished flow has five check screens and starts
// the work from the sixth; under the old rule the program in between really did
// have five presses that each install, and the only thing standing between that and
// a user was a failing test. A screen that MEASURES something is not automatically a
// screen that INSTALLS it, so which screen starts is now a field of the entry -
// Screen::startsTheWork - written once per flow beside the label that names it. The
// harness's "exactly one" check still stands behind it; it is a second line now
// rather than the only one.
PrimaryAction primaryActionFor(const Wizard* w, int screen, bool finished)
{
    if (!w || screen < 0 || screen >= screenCount(w))
        return kPrimaryClose;
    // Same override, and for the same reason, as primaryLabelFor()'s: once the work
    // has run there is nothing else the button could honestly offer. The two
    // functions have to agree about this case or the button would say Close and do
    // something else, so it is written the same way in both rather than one calling
    // the other and hiding which of them owns the rule.
    if (finished)
        return kPrimaryClose;
    switch (w->screens[screen].kind) {
    case kScreenInfo:  return kPrimaryAdvance;
    case kScreenCheck: return w->screens[screen].startsTheWork ? kPrimaryStart
                                                              : kPrimaryAdvance;
    case kScreenWork:  return kPrimaryNone;   // the button is hidden while it runs
    default:           return kPrimaryClose;  // kScreenDone
    }
}

bool nextAllowed(const Wizard* w, int screen)
{
    if (!w || screen < 0 || screen >= screenCount(w))
        return false;
    const Screen* s = &w->screens[screen];
    if (s->kind != kScreenCheck)
        return true;
    if (!s->blockNextWhenUnmet)
        return true;
    return s->satisfied;
}

// THE STEADY STATE OF THE PRIMARY BUTTON, IN ONE PLACE. See the block in gui.h for
// why it is a function at all; what follows is only the rule.
//
// The two halves are the two reasons this program ever greys that control, and they
// are different in kind: the first is about the RUN (/preview, or a machine this
// installer has already refused, has promised that nothing will be written, so the
// press that would write is refused), the second is about the SCREEN (without the
// WinUSB binding nothing works, so the screen that measures it will not be left).
// Either one is enough.
bool primaryEnabledFor(const Wizard* w, int screen, bool finished)
{
    if (primaryActionFor(w, screen, finished) == kPrimaryStart && w &&
        w->startBlockedNote)
        return false;
    return nextAllowed(w, screen);
}

// The index of the FIRST screen of a given kind, or -1. There is exactly one work
// screen and exactly one done screen in any flow this program builds, which is what
// makes "the work screen" a thing that can be asked for by name instead of by a
// number somebody wrote down twice - and "first" and "the one" are only the same
// thing while that holds. installer/verify's screen table suite counts both kinds
// over both flows, which is where that sentence stops being a hope. It counts those
// two kinds and no others on purpose: kScreenCheck is about to be five of them.
static int screenOfKind(const Wizard* w, ScreenKind kind)
{
    int n = screenCount(w);
    for (int i = 0; i < n; i++)
        if (w->screens[i].kind == kind)
            return i;
    return -1;
}

// "The screen we are on is one of these." An index outside the table answers false
// to every kind, which is exactly what the four `g_pageIndex == kPgX` tests this
// replaces did with an index outside their enum - so the degenerate case behaves
// the way it always has instead of quietly picking a first arm.
static bool onKind(ScreenKind kind)
{
    return g_wiz && g_screen >= 0 && g_screen < screenCount(g_wiz) &&
           g_wiz->screens[g_screen].kind == kind;
}

// The entry the window is on, or null when the index is outside the table. One
// place that bounds-checks it, because the alternative is the same three term
// condition copied beside every field anybody wants to read.
static const Screen* thisScreen(void)
{
    if (!g_wiz || g_screen < 0 || g_screen >= screenCount(g_wiz))
        return 0;
    return &g_wiz->screens[g_screen];
}

// "This is the old page 2." EVERYTHING that belongs to that one page and not to
// check screens in general asks this: the review rows, the walkthrough pane, the
// Zadig picture, the Check again button, the contextual action and the note beside
// them. Before a second check screen existed all six were gated on the KIND, which
// meant the day one was added it would have inherited the whole of page 2 - a
// duplicate of the page the owner complained about, with a Next button on it.
// See Screen::paintsMachineReview.
static bool onMachineReview(void)
{
    const Screen* s = thisScreen();
    return s != 0 && s->kind == kScreenCheck && s->paintsMachineReview;
}

// ---------------------------------------------------------------------------
// THE TWO QUESTIONS ASKED OF A SCREEN'S CONTENT, IN ONE PLACE EACH.
//
// Both used to be asked of the WIZARD - "does this flow have a pane", "does this flow
// have an action" - and both were then gated on onMachineReview(), so the answer was
// the same on every screen and the pane and the button could only ever be on one. A
// screen carries its own pane and its own offer now, so the questions are about the
// entry; and they are functions rather than expressions because three places ask each
// of them, and three copies of a null test is how a painter, a layout and a creation
// path come to disagree about whether a control exists.
//
// EMPTY IS THE SAME AS ABSENT for both. A screen whose paneText is an empty buffer -
// which is what a walkthrough that could not be captured leaves behind - gets no pane
// rather than a pane with nothing in it; a screen whose actionLabel is empty gets no
// button, which is the state a machine with nothing outstanding is in.
// ---------------------------------------------------------------------------
static const wchar_t* paneTextOf(const Screen* s)
{
    return (s && s->paneText && s->paneText[0]) ? s->paneText : 0;
}

static const wchar_t* actionLabelOf(const Screen* s)
{
    return (s && s->action && s->actionLabel && s->actionLabel[0]) ? s->actionLabel : 0;
}

// ---------------------------------------------------------------------------
// *** DOES /preview REFUSE THIS SCREEN'S ACTION? ONE EXPRESSION, FOUR ASKERS. ***
//
// The four are layout()'s greying, giveTheButtonsBack(), the WM_BCD_RDONE handler and
// the press itself. Four copies of this rule is how a button comes to be enabled and
// inert, or grey and pressable - and this file has already had the first of those, in a
// real button, measured.
//
// *** IT GATES ON THE CONSEQUENCE AND NOT ON THE CATEGORY, WHICH IS THE CORRECTION THIS
//     ROUND MAKES. *** The rule used to be "/preview greys every contextual button".
// That kept the promise and overshot it: on the Zadig screen it left `Open the Zadig
// page` grey beside a live blue link that opens the same page - one consequence, two
// controls, opposite answers. /preview promises that nothing is WRITTEN and nothing is
// REGISTERED; asking a browser to open a public address is neither, and the mode has
// always allowed the re-check, which reads the whole registry and enumerates processes.
// A mode that permits a registry sweep cannot coherently forbid opening a web page.
//
// A NULL SCREEN ANSWERS "REFUSED", like an entry that says nothing about itself. See
// Screen::actionOnlyOpensAPage for why the field is phrased so that silence is safe.
// ---------------------------------------------------------------------------
static bool previewRefusesAction(const Screen* s)
{
    if (!g_wiz || !g_wiz->startBlockedNote)
        return false;
    return !(s && s->actionOnlyOpensAPage);
}

// ...and the third of them, added by the round that made the painted address open
// itself. Same shape and same reason: the hit test, the cursor and the press all ask
// it, and three copies of a null test is how a region comes to change the cursor on a
// screen whose click it then refuses. See Screen::addressOpen in gui.h.
//
// EMPTY IS THE SAME AS ABSENT here too, and the pair has to agree: an opener with no
// address is a click target with nothing painted under it, and an address with no
// opener is a line that looks like a link and is not. Either alone is a defect, so
// neither alone is a link.
static int (*addressOpenOf(const Screen* s))(void*, const wchar_t*)
{
    return (s && s->addressOpen && s->addressUrl && s->addressUrl[0]) ? s->addressOpen
                                                                      : 0;
}

// ---------------------------------------------------------------------------
// *** THE NAMED DOOR, AND THE WHOLE OF "IT IS REACHABLE ONLY FROM THE UNMET STATE"
//     IS THIS ONE EXPRESSION. ***
//
// It is written here rather than at each of the three sites that ask it - the
// geometry, the layout's showOnly(), and the press - for the reason paneTextOf() and
// actionLabelOf() are: three copies of a condition is how a control comes to be
// visible on a screen that refuses its press, or invisible on one that would accept
// it. And it is asked of `satisfied` and not of nextAllowed(), which would be the
// same value read through a function that ALSO answers about screens with no door -
// an assertion whose two sides come from one derivation cannot fail.
//
// THE FOUR CLAUSES, EACH FOR A DIFFERENT REASON:
//   - a label and a function: a door with no function behind it is a control that
//     swallows a press, which is the class this redesign exists to remove;
//   - blockNextWhenUnmet: a door beside a Next that already works is a second Next;
//   - !satisfied: the wall is what makes it a door. On a bound machine Next is
//     allowed and this control is not on the screen at all.
// ---------------------------------------------------------------------------
static const wchar_t* overrideLabelOf(const Screen* s)
{
    if (!s || !s->override || !s->overrideLabel || !s->overrideLabel[0])
        return 0;
    if (!s->blockNextWhenUnmet || s->satisfied)
        return 0;
    return s->overrideLabel;
}

// Whether ANY entry asks for one, which is what decides that the control is created
// at all. Created once, on the window thread, before the first screen is shown: a
// control that had to appear when the user reached a screen would be a window created
// from wherever the navigation happens to be, and a control that appeared from a
// worker's message would be a window created on the wrong thread entirely.
static bool flowHasPane(const Wizard* w)
{
    int n = screenCount(w);
    for (int i = 0; i < n; i++)
        if (paneTextOf(&w->screens[i]))
            return true;
    return false;
}

static bool flowHasAction(const Wizard* w)
{
    int n = screenCount(w);
    for (int i = 0; i < n; i++)
        if (w->screens[i].action)
            return true;
    return false;
}

// ---------------------------------------------------------------------------
// WHETHER A SCREEN OFFERS THE CHOICE, IN ONE EXPRESSION, for the reason
// actionLabelOf() and overrideLabelOf() are one each: this is asked by the reserving
// pass, by the layout, by the state sync and by the press, and four copies of a
// condition is how a control comes to be visible on a screen that ignores it.
//
// BOTH LABELS AND THE FUNCTION, and each clause earns its place. A choice with only
// one option is not a choice; a choice with no function behind it is two controls that
// swallow a press, which is the class this redesign exists to remove.
static bool screenHasChoice(const Screen* s)
{
    return s != 0 && s->choose != 0 &&
           s->choiceLabels[0] != 0 && s->choiceLabels[0][0] != 0 &&
           s->choiceLabels[1] != 0 && s->choiceLabels[1][0] != 0;
}

// ...and whether ANY entry does, which is what decides the controls are created at
// all. It asks the same predicate rather than a field, so a flow whose entry offers
// half a choice creates no controls instead of one - see flowHasOverride() for why the
// existence of a control must not depend on anything a re-check can change.
static bool flowHasChoice(const Wizard* w)
{
    int n = screenCount(w);
    for (int i = 0; i < n; i++)
        if (screenHasChoice(&w->screens[i]))
            return true;
    return false;
}

// ...and the same question for the door, so the control is created once or not at
// all. It asks for the FUNCTION and not for overrideLabelOf(), deliberately: whether
// a machine is satisfied changes with every re-check, and a control created only on
// the machines that need it would be a window whose existence depended on a registry
// read taken before the window opened.
static bool flowHasOverride(const Wizard* w)
{
    int n = screenCount(w);
    for (int i = 0; i < n; i++)
        if (w->screens[i].override)
            return true;
    return false;
}

// ---------------------------------------------------------------------------
// *** WHETHER A PICTURE IS DECODED AT ALL, AND WHY THIS IS A UNION AND NOT A FIELD
//     TEST. ***
//
// The bitmaps are decoded ONCE, near the top of runWizard(), and again in
// onDpiChanged(); the renderers then draw whatever was decoded. So a picture has TWO
// gates - the one that decodes it and the one that draws it - and they were reading
// DIFFERENT THINGS: renderSubject() drew on Screen::showDevicePhoto and
// Screen::showZadigShot, while both decodes asked Wizard::showDevicePhoto and
// Wizard::zadigCaption. A screen that set its own flag on a flow whose Wizard field
// happened to be null therefore got NO PICTURE AND NO COMPLAINT - the identical
// silent content loss the round that taught the renderer to read those flags claimed
// to have closed. It worked only because setup.cpp happens to set both.
//
// So the screen's flag is now SUFFICIENT: asking for a picture on an entry is enough
// to have it decoded, whatever the flow says. These are the expressions every decode
// gate uses - runWizard(), onDpiChanged() and installer/verify's own window builder -
// so there is one answer to "is this picture wanted" and three callers of it, instead
// of three copies of a field test.
//
// THE Wizard FIELD IS STILL IN THE UNION, and that is not belt and braces: the
// OPENING screen's photograph is drawn by renderWelcome() from Wizard::showDevicePhoto
// and the machine review's screenshot by renderReview() from Wizard::zadigCaption.
// Those two renderers have not moved onto the table yet (Tasks 5 and 6), so dropping
// the flow's half here would take the picture off the two pages that have one today.
// When the last of them moves, the union loses its right hand side and nothing else
// about these functions changes.
//
// A FALSE ANSWER COSTS A PICTURE AND NEVER A SENTENCE. Both captions are written to
// be read without their picture, and both renderers skip a bitmap they have not got.
// ---------------------------------------------------------------------------
static bool flowNeedsPhoto(const Wizard* w)
{
    if (w->showDevicePhoto)
        return true;
    int n = screenCount(w);
    for (int i = 0; i < n; i++)
        if (w->screens[i].showDevicePhoto)
            return true;
    return false;
}

static bool flowNeedsZadigShot(const Wizard* w)
{
    if (w->zadigCaption)
        return true;
    int n = screenCount(w);
    for (int i = 0; i < n; i++)
        if (w->screens[i].showZadigShot)
            return true;
    return false;
}

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
static void releaseBitmap(HBITMAP* bmp, int* w, int* h)
{
    if (*bmp) {
        DeleteObject(*bmp);
        *bmp = 0;
    }
    *w = *h = 0;
}

static void releasePhoto()
{
    releaseBitmap(&g_photo, &g_photoW, &g_photoH);
}

static void releaseZadigShot()
{
    releaseBitmap(&g_shot, &g_shotW, &g_shotH);
}

// *** ONE DECODER FOR BOTH PICTURES, WHICH IS WHY THE SECOND ONE COST NOTHING. ***
// It was decodePhoto() with IDR_DEVICE_PHOTO_PNG and three file scope variables
// written into it. Everything below - the premultiply before the scale, the top down
// DIB, the exact target width - is what the device photograph already needed, and a
// second copy of ninety lines is how two pictures come to be resampled two different
// ways on the same page.
static bool decodePngResource(int resourceId, int targetW, HBITMAP* bmpOut,
                              int* wOut, int* hOut)
{
    releaseBitmap(bmpOut, wOut, hOut);
    if (targetW < 1)
        return false;

    const void* data = 0;
    DWORD       size = 0;
    if (!loadPayload(resourceId, &data, &size) || !data || !size)
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
                *bmpOut = bmp;
                *wOut   = (int)dstW;
                *hOut   = (int)dstH;
                ok      = true;
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

static bool decodePhoto(int targetW)
{
    return decodePngResource(IDR_DEVICE_PHOTO_PNG, targetW, &g_photo,
                             &g_photoW, &g_photoH);
}

// The Zadig screenshot on page 2. It is decoded at the width the page will draw it
// at, like the photograph, so the resampling is WIC's and not AlphaBlend's.
//
// *** WHY IT IS DRAWN AT ITS NATIVE 574 LOGICAL PIXELS AND NOT SHRUNK TO SIT BESIDE
//     ITS CAPTION. *** The page is 683 pixels wide at 96 DPI and the picture is 574
// of them, so a side by side arrangement would need the picture at roughly half size.
// The two strings this picture exists to be matched against - the list line and
// USB ID 1397 00BF - are 9 pixel text in the original; at half size they are not text
// any more, they are grey. A picture of the most dangerous step in the installation
// that cannot be read is worse than no picture, because it invites somebody to
// believe they have matched something they cannot see. So the caption goes UNDER it,
// full width, and the picture stays legible.
static bool decodeZadigShot(int targetW)
{
    return decodePngResource(IDR_ZADIG_SHOT_PNG, targetW, &g_shot,
                             &g_shotW, &g_shotH);
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
    // Monospaced like the other two panes, and for the same reason: the text in it
    // is say() output, laid out with indents and a numbered list that only line up
    // in a fixed pitch face.
    if (g_review)    SendMessageW(g_review,    WM_SETFONT, (WPARAM)g_fMono, TRUE);
    // *** THE TWO LEFT HAND BUTTONS, WHICH USED TO BE LEFT OUT OF THIS LIST. ***
    // "Check again" was created and never given a font, so it drew in the system
    // font while every other control in the window drew in a font built for the
    // CURRENT DPI - which is the one thing this file scales everything through S()
    // to avoid. It was invisible while it was the only such control. It stops being
    // invisible the moment a second button sits beside it in the same band and the
    // two disagree, and the width of the new one is worked out by MEASURING its
    // label, so measuring in one font and drawing in another would be a layout
    // computed from a lie.
    if (g_recheckBtn) SendMessageW(g_recheckBtn, WM_SETFONT, (WPARAM)g_fBody, TRUE);
    if (g_actionBtn)  SendMessageW(g_actionBtn,  WM_SETFONT, (WPARAM)g_fBody, TRUE);
    if (g_overrideBtn)
        SendMessageW(g_overrideBtn, WM_SETFONT, (WPARAM)g_fBody, TRUE);
    // ...and the two choice controls, in g_fBody, which is the SAME font
    // renderSubject() measures their labels with. Those two must be one font or the
    // space reserved is computed from a face the control does not draw in - the exact
    // defect the paragraph above records for "Check again", and here it would clip the
    // one sentence on the screen that says what the BCD2000 does not support.
    for (int i = 0; i < 2; i++)
        if (g_modelBtn[i])
            SendMessageW(g_modelBtn[i], WM_SETFONT, (WPARAM)g_fBody, TRUE);
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

// ---------------------------------------------------------------------------
// ONE LINE OF TEXT, DRAWN WHERE IT IS TOLD, NEVER WRAPPED AND NEVER CLIPPED.
//
// *** IT EXISTS FOR EXACTLY ONE CALLER AND THE RESTRICTION IS THE POINT. *** textBlock()
// above wraps, which is what every other block on every page wants: a longer sentence at
// a higher DPI moves what follows down instead of being cut. The download address is the
// one thing on these screens where wrapping is the FAILURE - an address read in two
// halves is an address retyped wrong, which is the whole reason the owner asked to see it
// as text - and where two different fonts have to sit on ONE line so that the lead costs
// no line of its own. That is 26 logical pixels at 144 DPI on a screen with 11 to spend.
//
// SO IT IS ONLY EVER CALLED AFTER THE WIDTH HAS BEEN MEASURED against the room there is,
// by the caller, which then records both numbers for installer/verify - see
// lastAddressWidths(). DT_SINGLELINE into a rectangle S(4000) wide can neither wrap nor
// clip; what it CAN do is run past the margin, and the recorded pair is what makes that a
// failing check instead of something to notice in a capture.
// ---------------------------------------------------------------------------
static int drawOneLine(HDC dc, HFONT font, COLORREF colour, const wchar_t* text,
                       int x, int y, bool measure)
{
    if (!text || !*text)
        return 0;
    SelectObject(dc, font);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    if (!measure) {
        RECT d = { x, y, x + S(4000), y + tm.tmHeight };
        SetTextColor(dc, colour);
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, text, -1, &d, DT_SINGLELINE | DT_NOPREFIX);
    }
    return tm.tmHeight;
}

// Draws a decoded picture on its panel and returns the height the panel uses,
// measured or not. The panel is what holds the picture off the page's background;
// the picture is blended and not blitted because both PNGs carry alpha.
//
// It returns its height even when there is nothing to draw, so a caller can lay the
// page out the same way whether or not the resource decoded - which is what makes
// "the text simply moves up" a property of the layout rather than a second code
// path. A null bitmap gives 0 and the caller adds no gap.
static int drawPicture(HDC dc, HBITMAP bmp, int w, int h, int x, int y, bool measure)
{
    if (!bmp || w < 1 || h < 1)
        return 0;
    if (!measure) {
        // S(2 * kPicturePad) and NOT 2 * S(kPicturePad): one MulDiv, so the plate is
        // the same number of pixels it has always been at every DPI. Two scalings of
        // a half would round apart from one scaling of the whole at some of them, and
        // the skip test below in renderSubject() compares against this same
        // expression.
        fillRect(dc, x, y, w + S(2 * kPicturePad), h + S(2 * kPicturePad),
                 CLR_PANEL_BG);
        HDC     mem = CreateCompatibleDC(dc);
        HGDIOBJ ob  = SelectObject(mem, bmp);
        BLENDFUNCTION bf;
        bf.BlendOp             = AC_SRC_OVER;
        bf.BlendFlags          = 0;
        bf.SourceConstantAlpha = 255;
        bf.AlphaFormat         = AC_SRC_ALPHA;
        AlphaBlend(dc, x + S(kPicturePad), y + S(kPicturePad), w, h, mem, 0, 0, w, h,
                   bf);
        SelectObject(mem, ob);
        DeleteDC(mem);
    }
    return h + S(2 * kPicturePad);
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
        drawPicture(dc, g_photo, g_photoW, g_photoH, m, y, measure);
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

    int footY  = h - block - S(kPageBottomPad);
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
    // The bottom of the fine print, and NOT that plus a margin. The margin is still
    // there and is still S(kPageBottomPad) - it is what footY subtracts above, so the
    // block is anchored that far off the bottom edge - but it is paper and not
    // content. It cost this page a scroll bar at 144 DPI for twelve pixels of nothing.
    return footY + block;
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

    // -----------------------------------------------------------------------
    // *** THE SCREEN'S OWN WORDS, ABOVE ITS ROWS, AND THE POSITION IS THE POINT. ***
    //
    // This page had none: it painted the flow's caption and then the flow's rows, so
    // the one entry that renders through here was the one entry in the table whose
    // bullets went nowhere. That was invisible while its only text was a report of the
    // machine. It stopped being acceptable when the screen became the one that says
    // what this program is about to write and what it will NOT touch, because the
    // second half of that is not a reading of anything and cannot be a row.
    //
    // ABOVE the rows and not below them, unlike renderSubject(), and the reason is
    // arithmetic rather than taste: this screen reports 639 logical pixels into a 398
    // strip at 96 DPI, so everything under the rows is behind a scroll. The sentence
    // that makes an Install press honest is not a sentence to put behind a scroll.
    //
    // SAME IDIOM AS renderSubject(), including the measured dot, so the two kinds of
    // check screen look like one program - and measured on the measuring pass like
    // every other block here, so the scroll range covers it instead of it being
    // painted over the rows.
    // -----------------------------------------------------------------------
    {
        const Screen* self = thisScreen();
        for (int i = 0; self && i < 4 && self->bullets[i]; i++) {
            int bx = m + S(14);
            if (!measure) {
                int lh = lineHeight(dc, g_fSmall);
                fillRect(dc, m + S(2), y + lh / 2 - S(2), S(4), S(4), CLR_ACCENT);
            }
            y += textBlock(dc, g_fSmall, CLR_TEXT, self->bullets[i], bx, y,
                           w - 2 * m - S(14), measure);
            y += S(8);
        }
    }

    for (int i = 0; i < g_wiz->reviewCount && i < kMaxRows; i++)
        y += drawRow(dc, &g_wiz->review[i], m, y, w - 2 * m, measure, false);

    // *** THE ZADIG SCREENSHOT IS NOT ON THIS PAGE ANY MORE, AND THAT IS THE HALF OF
    //     THIS ROUND THE OWNER ACTUALLY COMPLAINED ABOUT. ***
    //
    // It stood here, under the rows, with a three paragraph caption under it - and
    // the at-rest capture of this page measured what that meant: at 96 DPI the last
    // row was cut mid sentence at the bottom edge and the picture of the operation
    // this program itself calls "the most dangerous operation in the whole
    // installation" was ENTIRELY below the fold on arrival, with its caption, the
    // account row and the footer. 63 per cent of this page needed scrolling at 96 DPI
    // and 65 at 144.
    //
    // The picture has a screen of its own now and renderSubject() draws it, from
    // Screen::showZadigShot, above the fold and measured as such. Nothing about the
    // picture, the caption or the rule that skips a picture too wide for its page was
    // changed on the way: the block moved, it did not shrink. See renderSubject().
    //
    // *** AND THE TWO ROWS THAT WERE "DELIBERATELY STILL HERE" ARE GONE. *** This
    // block used to argue that the third party virtual MIDI row and the WinUSB row
    // should stay even though both had screens of their own, because this page is a
    // REPORT and a report with a hole where its most important line used to be is
    // worse than a report that repeats a screen. The argument expired rather than
    // being overruled: the screen this page became states both subjects in its own
    // words, as the two things it reads and will not change. See fillPreflightRows().
    //
    // *** ONE THING DID LEAVE THE WINDOW WITH THE ROWS, AND THIS BLOCK SAID IT DID
    //     NOT. *** It read "nothing was lost by dropping the rows". That was false and
    // it is corrected here rather than softened, which is this project's rule for a
    // text that claims more than was measured - the eleventh such text, and the first
    // to be written by the round that created the gap.
    //
    // *** AND THE THIRD PARTY DETECTION ITSELF IS GONE NOW, WHICH IS A LATER ROUND
    //     THAN THE ONE THIS BLOCK DESCRIBES. *** The MIDI port screen's panel used to
    // name the literal path the control service opened and explain why it was a
    // literal path rather than a search - see the round that removed that detection
    // for where the replacement lands.

    if (g_wiz->reviewFooter) {
        y += S(4);
        if (!measure)
            fillRect(dc, m, y, w - 2 * m, 1, CLR_LINE);
        y += S(12);
        y += textBlock(dc, g_fSmall, CLR_DIM, g_wiz->reviewFooter, m, y,
                       w - 2 * m, measure);
    }

    // The device nudge. It appears only after Windows has said something arrived or
    // left, and it is measured here like every other block on the page, so it takes
    // room of its own and the scroll range grows to cover it instead of it being
    // painted over the footer.
    if (g_deviceChanged) {
        y += S(10);
        y += textBlock(dc, g_fBodyBold, CLR_WARN, kDeviceNudge, m, y, w - 2 * m,
                       measure);
    }
    // The bottom of the last block, and NOT that plus a margin. See kPageBottomPad:
    // layout() adds the paper back to the scroll RANGE, on a page that scrolls.
    return y;
}

// ---------------------------------------------------------------------------
// A CHECK SCREEN THAT CARRIES ITS OWN SUBJECT: its title, the one row it measured,
// and its bullets. Nothing else - no pane, no picture, no second subject.
//
// *** THIS IS WHAT MAKES A NEW SCREEN A SCREEN RATHER THAN A SECOND COPY OF PAGE 2.
//     *** Every content field of Screen has existed since the table was introduced
// and NOTHING read them: the renderer painted from the Wizard, so the first entry
// somebody added of kind kScreenCheck would have drawn the machine review again,
// word for word, with a Next button under it. That is not a hypothetical - it is
// what this function's absence would have shipped in this round, and it is why the
// task that moves a subject onto a screen is also the task that teaches the
// renderer to paint one.
//
// The layout is deliberately the same shape the two pages it borrows from already
// use - a title, a rule, then blocks measured with textBlock() - so that nothing
// here can be clipped at any DPI for a reason the other pages do not share.
// ---------------------------------------------------------------------------
static int renderSubject(HDC dc, int w, int h, bool measure)
{
    (void)h;
    const Screen* s = thisScreen();
    if (!s)
        return 0;

    int m = S(kMargin);
    int y = S(22);

    y += textBlock(dc, g_fTitle, CLR_TEXT, s->title, m, y, w - 2 * m, measure);
    y += S(14);
    if (!measure)
        fillRect(dc, m, y, w - 2 * m, 1, CLR_LINE);
    y += S(14);

    // WHAT WAS FOUND, first, and in the same row shape page 2 uses - the mark, the
    // title and the sentence. A screen whose answer is below its explanation is a
    // screen somebody has to read to the end before learning whether it applies.
    //
    // *** AND A SCREEN THAT MEASURED NOTHING GETS NO ROW AT ALL, WHICH IS NOT THE
    //     SAME AS AN EMPTY ONE. *** "Get Zadig" is kScreenInfo and has no reading to
    // report: Zadig installs nothing and leaves no key behind, so a row there would
    // be a mark this program cannot honestly paint - and a zeroed Row draws a
    // coloured square beside a blank line, which is a page reporting on something it
    // did not measure. The title is what says whether there is one, because setRow()
    // is the only thing that ever writes it.
    if (s->row.title[0])
        y += drawRow(dc, &s->row, m, y, w - 2 * m, measure, false);

    // -----------------------------------------------------------------------
    // *** THE DOWNLOAD ADDRESS, AND IT IS HERE - AFTER WHAT THIS IS AND WHAT WAS
    //     FOUND, BEFORE EVERYTHING ELSE - BECAUSE THE OWNER PUT IT HERE. ***
    //
    // He ran the program, looked at this screen and the Zadig one, and said the links
    // have to come OUT of the instruction box and sit somewhere obvious ABOVE the
    // summary text. His screenshot is the evidence: the address on this screen was
    // SELECTED with the mouse, inside a pane that scrolls. See Screen::addressLead
    // in gui.h for
    // the two decisions he took on mock-ups and for why this block is conditional.
    //
    // *** THE ADDRESS IS DRAWN AS ITS OWN LINE, IN THE ACCENT COLOUR, AND THE LEAD
    //     ABOVE IT IS DIM. *** Two blocks and not one interpolated string, because the
    // address is the thing to be found and a sentence it is buried inside is the shape
    // this whole task exists to undo. The accent colour is the one this window already
    // uses for an address - renderWelcome() draws kRepositoryUrl in it - so a reader
    // who has seen the opening page has already been taught what that colour means.
    //
    // *** IT IS MEASURED, LIKE EVERY OTHER BLOCK ON THIS PAGE, AND THE MEASUREMENT IS
    //     RECORDED. *** The strip this function paints into is the page MINUS the pane,
    // and on the MIDI port screen that strip has 20 logical pixels of slack at 96 DPI
    // and 11 at 144. A block that took room without reporting it would be the defect
    // the owner saw once already - content passing underneath the pane - so the box is
    // recorded and installer/verify asserts its bottom against the strip the layout
    // really gave this screen.
    //
    // AND SO IS THE WIDTH THE ADDRESS NEEDED. g_addrNeedW is the address on ONE line in
    // the font it is drawn in; g_addrHaveW is the width its own line really gives it. An
    // address wider than that is an address read in two halves, and an address read in
    // two halves is an address retyped wrong. That is a number here rather than
    // something to notice in a capture.
    //
    // *** THE LEAD SHARES THE ADDRESS'S LINE, AND THE REASON IS A MEASUREMENT THAT
    //     KILLED THE OBVIOUS ARRANGEMENT. *** Written as two stacked blocks - the lead
    // dim above, the address in the accent colour below - this cost 44 logical pixels at
    // 96 DPI and 69 at 144. The Zadig screen, which the plan named as the CHEAP place
    // with 53 and 60 pixels of slack, went 9 pixels over its strip at 144 DPI; the MIDI
    // port screen went 23 over even after the pointer bullet had been dropped to pay for
    // it. Rule 1 caught both, loudly, on a named list. On one line the block is 31 and
    // 46, and the lead is FREE - which is what makes an arrangement with words in it
    // affordable on a screen that had 11 pixels. (It read 47 until this round. The
    // harness prints the figure on every run - "it costs %d out of this strip's slack" -
    // and it is 46 at 144 DPI on both screens.)
    //
    // BASELINES ALIGNED AND NOT TOPS. The two fonts differ in height, so drawing both at
    // the same y would sit the lead's small text visibly high against the address. The
    // shorter ascent drops by the difference, which is asked of GetTextMetricsW rather
    // than assumed - and the arithmetic is written for the case where the SMALL font has
    // the taller ascent too, because a font substitution on somebody else's machine is
    // not this file's to promise.
    //
    // AND IT FALLS BACK TO STACKING RATHER THAN OVERFLOWING. If the lead and the address
    // do not both fit, they are drawn as two wrapped blocks - which costs a line and is
    // caught by Rule 1 - instead of being run past the margin, which nothing would catch.
    // -----------------------------------------------------------------------
    g_addrShown = false;
    g_addrNeedW = 0;
    g_addrHaveW = 0;
    ZeroMemory(&g_addrBox, sizeof(g_addrBox));
    // *** AND THE HIT REGION IS ZEROED WITH THEM, WHICH IS THE WHOLE OF "NO ADDRESS, NO
    //     LINK". *** The click handler asks this rectangle and nothing else, so a screen
    // that draws no address leaves behind no place on the page that opens a browser. A
    // stale rectangle here would be a click on blank paper doing something, on a screen
    // whose whole point in the satisfied state is that it asks for nothing.
    g_addrLink  = false;
    ZeroMemory(&g_addrLinkBox, sizeof(g_addrLinkBox));
    if (s->addressUrl && s->addressUrl[0]) {
        int avail = w - 2 * m;
        y += S(6);
        g_addrBox.left = m;
        g_addrBox.top  = y;

        SIZE        uz;
        TEXTMETRICW utm;
        ZeroMemory(&uz, sizeof(uz));
        SelectObject(dc, g_fBody);
        GetTextExtentPoint32W(dc, s->addressUrl, (int)wcslen(s->addressUrl), &uz);
        GetTextMetricsW(dc, &utm);

        SIZE        lz;
        TEXTMETRICW ltm;
        ZeroMemory(&lz, sizeof(lz));
        ZeroMemory(&ltm, sizeof(ltm));
        if (s->addressLead && s->addressLead[0]) {
            SelectObject(dc, g_fSmall);
            GetTextExtentPoint32W(dc, s->addressLead, (int)wcslen(s->addressLead), &lz);
            GetTextMetricsW(dc, &ltm);
        }

        int leadRoom = (lz.cx > 0) ? lz.cx + S(8) : 0;
        bool sameLine = (leadRoom + uz.cx <= avail);
        g_addrNeedW   = uz.cx;
        g_addrHaveW   = sameLine ? avail - leadRoom : avail;

        // *** THE HIT REGION IS TAKEN FROM THE SAME ARITHMETIC THAT DRAWS THE LETTERS,
        //     AND THAT IS WHY IT IS RECORDED HERE AND NOT COMPUTED BY THE CLICK
        //     HANDLER. *** A handler that re-derived where the address is would be a
        // second author of one rectangle, and the two would part company the first time
        // either branch below changed - a click landing beside the words it looks like
        // it is on. So the renderer writes down what it drew and the handler reads it.
        // See lastAddressLinkBox() in gui.h for why it is the address's own run and not
        // the block.
        if (sameLine) {
            int drop  = (int)utm.tmAscent - (int)ltm.tmAscent;
            int leadY = y + (drop > 0 ? drop : 0);
            int urlY  = y + (drop < 0 ? -drop : 0);
            int hLead = drawOneLine(dc, g_fSmall, CLR_DIM, s->addressLead, m, leadY,
                                    measure);
            int hUrl  = drawOneLine(dc, g_fBody, CLR_ACCENT, s->addressUrl,
                                    m + leadRoom, urlY, measure);
            g_addrLinkBox.left   = m + leadRoom;
            g_addrLinkBox.top    = urlY;
            g_addrLinkBox.right  = m + leadRoom + uz.cx;
            g_addrLinkBox.bottom = urlY + hUrl;
            int block = (leadY - y) + hLead;
            if ((urlY - y) + hUrl > block)
                block = (urlY - y) + hUrl;
            y += block;
        } else {
            if (s->addressLead)
                y += textBlock(dc, g_fSmall, CLR_DIM, s->addressLead, m, y, avail,
                               measure);
            // THE STACKED FALLBACK, where the address is wrapped over as many lines as
            // it needs. The region is the whole of what was wrapped: an address read in
            // halves is already the failure this arrangement exists to avoid, and the
            // check that holds the one-line form is in installer/verify - here the job
            // is only to make sure the region never covers anything the address did not
            // draw on. It is never wider than the column and never taller than the block.
            int urlTop = y;
            y += textBlock(dc, g_fBody, CLR_ACCENT, s->addressUrl, m, y, avail, measure);
            g_addrLinkBox.left   = m;
            g_addrLinkBox.top    = urlTop;
            g_addrLinkBox.right  = m + (uz.cx < avail ? uz.cx : avail);
            g_addrLinkBox.bottom = y;
        }

        g_addrBox.right  = m + avail;
        g_addrBox.bottom = y;
        g_addrShown      = true;
        // *** AND IT IS A LINK ONLY WHEN THERE IS SOMETHING FOR A CLICK TO RUN. *** The
        // rectangle is drawn from the letters; whether it ACTS is the entry's own
        // answer. A screen that painted an address and supplied no opener would leave a
        // region that changes the cursor and then does nothing when pressed, which is
        // worse than a region that was never a link.
        g_addrLink       = (s->addressOpen != 0);
        y += S(8);
    }

    // -----------------------------------------------------------------------
    // *** THE TWO CHOICE CONTROLS: SPACE IS RESERVED HERE, THE WINDOWS ARE PLACED BY
    //     layout(), AND NOTHING IS PAINTED EITHER WAY. ***
    //
    // IT IS DIRECTLY UNDER THE ROW, and that order is the same judgement the row
    // itself rests on: the row says what this machine WAS FOUND to have, and the
    // choice is what will be assumed about it. A question above its own answer is a
    // screen somebody answers before reading what was already known.
    //
    // *** THE LABELS ARE MEASURED AND NEVER DRAWN BY THIS FUNCTION, WHICH IS THE ONE
    //     THING TO GET RIGHT HERE. *** BS_AUTORADIOBUTTON draws its own text. A
    // textBlock() painted underneath it would be the same sentence twice, half a pixel
    // apart, at every DPI - so these calls pass `true` for measure UNCONDITIONALLY and
    // the `measure` parameter is deliberately not forwarded. That asymmetry is the
    // whole reason this block looks different from every other block in this function.
    //
    // WHAT IS RESERVED IS WHAT layout() USES. Both are recorded, so the placement is
    // not a second piece of arithmetic that can disagree with the measurement - which
    // is exactly how the Zadig picture's recorded box came to describe a rectangle
    // nothing drew. installer\verify reads the real control windows back and asserts
    // they are inside the box this pass reserved.
    // -----------------------------------------------------------------------
    g_choiceShown = false;
    ZeroMemory(&g_choiceBox, sizeof(g_choiceBox));
    ZeroMemory(g_choiceRow, sizeof(g_choiceRow));
    if (screenHasChoice(s)) {
        int avail  = w - 2 * m;
        int labelW = avail - S(kChoiceIndW);
        if (labelW < S(80))
            labelW = S(80);
        int floorH = lineHeight(dc, g_fBody);

        y += S(kChoiceLeadH);
        g_choiceBox.left = m;
        g_choiceBox.top  = y;
        for (int i = 0; i < 2; i++) {
            // Measured WRAPPED, in the width the label really gets, so the longest
            // sentence either option carries moves the option below it down rather
            // than being cut. See kChoiceLeadH.
            int textH = textBlock(dc, g_fBody, CLR_TEXT, s->choiceLabels[i],
                                  m + S(kChoiceIndW), y, labelW, true);
            int rowH  = textH + 2 * S(kChoicePadH);
            if (rowH < floorH + 2 * S(kChoicePadH))
                rowH = floorH + 2 * S(kChoicePadH);
            g_choiceRow[i].left   = m;
            g_choiceRow[i].top    = y;
            g_choiceRow[i].right  = m + avail;
            g_choiceRow[i].bottom = y + rowH;
            y += rowH;
            if (i == 0)
                y += S(kChoiceGap);
        }
        g_choiceBox.right  = m + avail;
        g_choiceBox.bottom = y;
        g_choiceShown      = true;
    }

    // -----------------------------------------------------------------------
    // *** THE ZADIG SCREENSHOT, AND IT IS DRAWN HERE - ABOVE THE BULLETS - FOR ONE
    //     MEASURED REASON. ***
    //
    // This screen exists so that the picture is on the screen when it OPENS. Drawn
    // after the bullets, at 96 DPI it starts at about 236 logical pixels and its 254
    // put its bottom past the 398 the page has: it would be the old defect on a new
    // screen, which is the accumulation this whole redesign is against. Above them it
    // starts at about 106 and ends at about 360, and installer/verify asserts that
    // bottom against the strip the layout really gave this screen rather than against
    // a number written here.
    //
    // The bullets go BELOW it and may need a scroll until Task 6b, and that is the
    // right way round: they are the procedure, read while looking at the picture, and
    // the picture is what somebody has to see before they believe they have matched
    // anything.
    //
    // *** THE CAPTION IS PART OF THE PICTURE AND NOT PART OF THE PAGE. *** It says
    // what to match in that window and, at more length, what will NOT match, because
    // the screenshot was taken on a machine that is already bound. It is drawn
    // whether or not the picture decoded - it names every field by its own label and
    // never by where it sits in the image - so a resource that cannot be decoded
    // costs the picture and not a sentence. Every fact in it is ALSO said through
    // say() by the walkthrough, which is what puts it in the console, the log file, a
    // screen reader and the clipboard; painted text is the one kind of text in this
    // program that reaches none of those.
    //
    // *** AND THE PICTURE IS SKIPPED RATHER THAN SQUEEZED WHEN THE PAGE IS TOO NARROW
    //     FOR IT. *** Same expression renderReview() used to carry, moved with the
    // block it belongs to. A shrunk screenshot of this particular window is not
    // readable - the two strings it exists to be matched against are 9 pixel text -
    // and an unreadable picture of the most dangerous step invites somebody to
    // believe they have matched something they cannot see, which is worse than no
    // picture. The skip is SILENT, so it is recorded in g_shotDrawn and
    // installer/verify fails on it rather than a page quietly losing its subject.
    // -----------------------------------------------------------------------
    if (s->showZadigShot) {
        int avail = w - 2 * m;
        y += S(10);
        g_shotDrawn = (g_shot != 0 && g_shotW + S(2 * kPicturePad) <= avail);
        if (g_shotDrawn) {
            // *** THE RECTANGLE AlphaBlend WRITES, AND NOT THE HYBRID THIS USED TO
            //     RECORD. *** drawPicture() lays a plate of CLR_PANEL_BG at (m, y),
            // S(16) larger than the image in each axis, and blends the image itself
            // S(8) inside it. This took its origin from the plate and its size from
            // the image, so it described a rectangle nothing draws and every slack
            // measured off it was S(8) too generous - on the one screen whose whole
            // purpose is that the picture opens above the fold. See gui.h.
            //
            // kPicturePad is drawPicture()'s own inset, named rather than repeated:
            // a literal 8 here would be a second copy of a number that lives there,
            // and this whole finding is about a rectangle that disagreed with the one
            // being painted.
            g_shotBox.left   = m + S(kPicturePad);
            g_shotBox.top    = y + S(kPicturePad);
            g_shotBox.right  = m + S(kPicturePad) + g_shotW;
            g_shotBox.bottom = y + S(kPicturePad) + g_shotH;
            y += drawPicture(dc, g_shot, g_shotW, g_shotH, m, y, measure);
            y += S(10);
        } else {
            ZeroMemory(&g_shotBox, sizeof(g_shotBox));
        }
        if (g_wiz->zadigCaption)
            y += textBlock(dc, g_fSmall, CLR_DIM, g_wiz->zadigCaption, m, y, avail,
                           measure);
    }

    // ...and then why, in the screen's own words. Same bullet idiom as the opening
    // page, including the measured dot, so the two look like one program.
    y += S(6);
    for (int i = 0; i < 4 && s->bullets[i]; i++) {
        int bx = m + S(14);
        if (!measure) {
            int lh = lineHeight(dc, g_fSmall);
            fillRect(dc, m + S(2), y + lh / 2 - S(2), S(4), S(4), CLR_ACCENT);
        }
        y += textBlock(dc, g_fSmall, CLR_TEXT, s->bullets[i], bx, y,
                       w - 2 * m - S(14), measure);
        y += S(8);
    }

    // -----------------------------------------------------------------------
    // AND THE OTHER PICTURE A SCREEN MAY ASK FOR, READ OFF THE ENTRY.
    //
    // *** THIS IS THE HALF OF THE TABLE THAT WAS DECLARED AND UNREAD. *** Screen has
    // carried showDevicePhoto and showZadigShot since the table was introduced and
    // NOTHING looked at either: the photograph was drawn from a Wizard field by
    // renderWelcome() and the screenshot from another one by renderReview(), so a
    // screen that asked for a picture got no picture and no complaint. Content that
    // vanishes in silence is the class this project has paid for four times, and the
    // round that gave a screen a pane is the round that closed it for all three
    // content fields at once rather than for the one it happened to need.
    //
    // MEASURED LIKE EVERY OTHER BLOCK ON THIS PAGE, which is what keeps a picture and
    // a pane off one another. The pane is a child window fixed to the bottom of the
    // page and layout() takes its height out of the strip this function paints into;
    // a picture that took room without reporting it would be a picture the strip does
    // not know about, which is exactly the defect the owner saw - the Zadig shot
    // passing underneath the pane.
    //
    // IT IS LAST AND THE SCREENSHOT IS NOT, and that asymmetry is measured rather
    // than stylistic: the photograph is 210 wide and identifies a device somebody is
    // holding, so it can be met at the end of a screen; the screenshot is 254 TALL
    // and is the subject of the screen it is on. See the block above the bullets.
    //
    // A PICTURE THAT COULD NOT BE DECODED IS SKIPPED WITHOUT A WORD. drawPicture()
    // returns 0 for a null bitmap, so the null case needs no test of its own.
    // -----------------------------------------------------------------------
    if (s->showDevicePhoto && g_photo) {
        y += S(10);
        y += drawPicture(dc, g_photo, g_photoW, g_photoH, m, y, measure);
    }
    // The bottom of the last block, and NOT that plus a margin. See kPageBottomPad -
    // this is the return that cost the MIDI port screen a scroll bar on top of its
    // pane at 144 DPI, for 44 rows of paper nobody needs to reach.
    return y;
}

// The box the screenshot was last laid out in. See lastZadigShotBox() in gui.h: it
// answers false when the picture was skipped, which is the one thing this page does
// without saying so.
bool lastZadigShotBox(RECT* out)
{
    if (out)
        *out = g_shotBox;
    return g_shotDrawn;
}

// The box the two choice controls were last RESERVED in. See lastModelChoiceBox() in
// gui.h: it answers false on every screen that offers no choice, which is every screen
// but one.
bool lastModelChoiceBox(RECT* out)
{
    if (out)
        *out = g_choiceBox;
    return g_choiceShown;
}

// The box the download address was last drawn in, and what it needed against what it
// had. See lastAddressBox() in gui.h: it answers false on every screen that says
// nothing about downloading, which is every screen in both flows but two.
bool lastAddressBox(RECT* out)
{
    if (out)
        *out = g_addrBox;
    return g_addrShown;
}

// The part of that block a click opens. See lastAddressLinkBox() in gui.h: it is the
// accent-coloured run alone, and it answers false both on a screen with no address and
// on a screen whose entry supplied nothing for a click to run.
bool lastAddressLinkBox(RECT* out)
{
    if (out)
        *out = g_addrLinkBox;
    return g_addrLink;
}

// The address's own width on one line, and the width of the column it was given. See
// the block over these in renderSubject(): textBlock() wraps rather than clips, so an
// address that does not fit is an address read in halves.
void lastAddressWidths(int* needW, int* haveW)
{
    if (needW)
        *needW = g_addrNeedW;
    if (haveW)
        *haveW = g_addrHaveW;
}

// The one dispatch for a check screen, so that the window's painter, the window's
// layout and installer/verify's camera cannot answer this question differently.
// All three called renderReview() directly before there was a second kind of check
// screen; three copies of a two way branch is how a picture comes to be taken of a
// page the product does not draw.
static int renderCheckScreen(HDC dc, int w, int h, bool measure)
{
    return onMachineReview() ? renderReview(dc, w, h, measure)
                             : renderSubject(dc, w, h, measure);
}

// ...and the same dispatch for the OTHER kind that has two renderers now. It is one
// function for the reason renderCheckScreen() is one: the window's painter, the
// window's layout and installer/verify's camera all ask it, and three copies of a two
// way branch is how a picture comes to be taken of a page the product does not draw.
//
// A kScreenInfo screen paints the WELCOME - the Wizard's lines, its bullets, the
// credits and the non-affiliation notice - only when its entry says so. Every other
// Info screen carries its own words and gets renderSubject(), which draws them and
// skips the row it has not got. Before this round the kind alone decided, and the
// second Info screen in the flow drew the opening page instead of itself.
static int renderInfoScreen(HDC dc, int w, int h, bool measure)
{
    const Screen* s = thisScreen();
    return (s && s->paintsOpening) ? renderWelcome(dc, w, h, measure)
                                   : renderSubject(dc, w, h, measure);
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
// tmHeight is tmAscent + tmDescent: the font's STANDARD CELL, descenders included.
// That is the right yardstick here and it is why the metric is asked for instead of
// a number being picked - but it is not a universal bound, and this comment used to
// claim it was. Glyph ink can exceed tmHeight VERTICALLY: an accent over a capital,
// a writing system with marks above and below. (tmOverhang used to be listed here as
// a third example and it does not belong: it is a HORIZONTAL measurement, the amount
// a synthesised bold or italic adds to a string's WIDTH, and it says nothing about
// how far ink reaches up or down.) Nothing in this program does any of those - both
// strings are Latin ASCII in an upright system face - so the cell is the whole of the
// ink here, which is measured rather than assumed: installer/verify compares the ink
// of the painted band against the ink the same font produces with nothing to clip it.
// If a face or a string ever did put ink outside the cell, that comparison would FAIL
// instead of clipping quietly, which is the reason it is written as a measurement.
//
// kHeadH is now a FLOOR and no longer the height. At 96 DPI with the system message
// font the arithmetic below lands on exactly 70, so the band is exactly as TALL as
// the one the owner has already seen; at 144 it asks for 108 where 1.5 x 70 is 105,
// and it gets 108. THE TEXT INSIDE IT DID MOVE, and the owner will see that: at 96
// the headline's box went from 14..40 to 12..42 and the subhead's from 42..62 to
// 44..59, so the headline sits 2 rows higher and the subhead 2 rows lower in a box
// 5 rows shorter. That IS the correction - the boxes now match the fonts' cells -
// so the band is the same height with its two lines in slightly different places,
// not a band identical to the pixel.
//
// WHAT THE FLOOR IS FOR, WHICH IS NOT "the band may legitimately be taller than its
// parts". It is a guard for the degenerate case: the tm below is zeroed and stays
// zeroed if the DC is unusable, which would otherwise ask for a band of 25 pixels
// with two boxes of no height in it. In ordinary operation the parts win and the
// floor never engages - at 96 DPI they tie exactly at 70, at 144 they beat it by 3 -
// and installer/verify ASSERTS that, so a change that quietly moved one of the three
// paddings into dead space under the floor is a red line rather than nothing. The
// margin is one pixel wide: the two font cells sum to exactly 45 logical pixels at
// 96 DPI, and a face whose cells summed to less would trip that assertion. Said here
// so the two files stop disagreeing about what this constant means.
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
    // tm IS ZEROED BECAUSE A CALLER CAN PASS A DC THAT IS NULL. Of the two callers,
    // ONE gets its DC from GetDC(): layout(), which returns 0 when the process has
    // run out of GDI handles. paintFrameInto() does not - it is handed a DC by its
    // caller, which is BeginPaint() in paintFrame() and a memory DC in the
    // verification harness, so the sentence that used to sit here named the wrong
    // source for both. Either way a null or unusable DC makes SelectObject and
    // GetTextMetricsW fail and leave tm exactly as they found it, so an unzeroed
    // tm.tmHeight would be whatever was on the stack, and a large enough value would
    // put the band, and therefore the page and the buttons, outside the window.
    //
    // WHAT ZEROED ACTUALLY PRODUCES, corrected: not "two lines overlapping at the
    // top". tmHeight 0 makes title.bottom equal title.top and sub.bottom equal
    // sub.top, so DrawTextW is given two boxes of NO HEIGHT and clips both to
    // nothing. The band is kHeadH tall and EMPTY. Ugly and deterministic, which is
    // the property that was wanted; readable it is not. Unlikely to happen, fully
    // predictable once it does.
    ZeroMemory(&tm, sizeof(tm));
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

// ---------------------------------------------------------------------------
// THE FOOT BAND, for the same reason headBand() exists: one piece of arithmetic
// used by the painter, by the layout and by the measurement in installer/verify.
//
// WHY THE "CHECK AGAIN" BUTTON IS HERE AND NOT ON THE PAGE. The page scrolls, and
// scrollTo() deliberately does NOT pass SW_SCROLLCHILDREN, so a button placed on the
// page would sit still while the rows slid under it. The alternative - scrolling the
// children too - puts the button off the top of the window as soon as the user reads
// the last row, and a button you have to scroll back up to find is a button you do
// not know is there. The foot band is the one strip of this window that never moves.
//
// "The page scrolls" is still TRUE, and the round that expected to be able to delete
// that sentence is the round that measured why it cannot: two screens overflow their
// page by content rather than by padding, and one of them is the binding screen, which
// is where "Check again" and the named door stand together. See the block on
// Wizard::action in gui.h for the arithmetic and where it is published.
//
// WHY THE NOTE BESIDE IT NOW WRAPS. The note used to have the whole width of the
// foot on one DT_SINGLELINE | DT_END_ELLIPSIS line, and NOTHING measured it: it is
// painted by the frame, outside every pixel the page proof looks at. At 96 DPI that
// line was about 364 logical pixels wide and the longest note either program sets is
// 63 characters, so it was ALREADY being cut - quietly, and found by arithmetic
// rather than by an eye. Two wrapped lines in the room the button leaves is about
// 2 x 270, which is more room than one line of 364 was, so every note is better off
// than it was; and installer/verify now measures what the note needs against what
// its box gives, so the next longer sentence fails a check instead of losing a word.
struct FootNote {
    RECT           box;      // what DrawTextW is given
    int            needH;    // what the text needs in that width, wrapped
    const wchar_t* text;     // null when nothing is painted there
};

// ---------------------------------------------------------------------------
// The contextual action button's geometry, and it is a function for the same
// reason headBand() and footNote() are: ONE piece of arithmetic, used by the
// layout, by the note's box and by the measurement in installer/verify. Two copies
// of it is how a painter and a layout come to disagree.
//
// The width comes from the WORDS, because the words change with what is being
// offered - a screen's entry decides its own label, whatever it names. needW is
// kept beside w so that a label which does not fit is a number somebody can fail
// a check on, rather than a truncation nobody sees.
struct ActionBand {
    bool shown;
    int  x, w;               // in the frame's client coordinates
    int  needW;              // what the label needs, padding included
};

// ---------------------------------------------------------------------------
// *** IS "Check again" IN THIS BAND, ON THIS SCREEN. ONE DEFINITION, AND IT WAS THREE
//     EXPRESSIONS THAT DID NOT ALL SAY THE SAME THING. ***
//
// layout() decides whether to SHOW the re-check button with the whole condition below,
// and actionBand() and overrideBand() decided where to put themselves from `g_wiz->recheck`
// alone - "does this FLOW have a re-check", which is not the same question. The two
// answers were identical for as long as every screen that carried a contextual button was
// also a kScreenCheck, and the round that put an "Open the Zadig page" button on "Get
// Zadig" - a kScreenInfo, where the re-check button is hidden - is the round they part
// company: the offer would have been pinned S(kRecheckW) + S(kActionGap) to the right of
// a button that is not on that screen, leaving 114 logical pixels of hole at the left of
// the band and no check able to see it, because the only assertion on that x compared it
// against a Check again the screen does have.
//
// So it is one function, asked by all three. It is DATA and not a window read - the same
// distinction the comment in actionBand() draws: a layout that asked the windows where
// they are would be reading back its own answer from the last time it ran.
// ---------------------------------------------------------------------------
static bool recheckStandsHere(void)
{
    return g_wiz && g_wiz->recheck != 0 && onKind(kScreenCheck) &&
           !g_working && !g_finished;
}

// Where the left hand side of the foot band starts for whatever goes in it first. One
// expression, for the reason above.
static int leftBandStart(void)
{
    return recheckStandsHere() ? S(24) + S(kRecheckW) + S(kActionGap) : S(24);
}

// One place that writes g_actionLabel, so the bound is applied once. A null or
// empty label is the "there is nothing to offer" state and takes the button away.
static void setActionLabel(const wchar_t* label)
{
    if (!label || !label[0]) {
        g_actionLabel[0] = 0;
        return;
    }
    wcsncpy(g_actionLabel, label, 127);
    g_actionLabel[127] = 0;
}

static ActionBand actionBand(HDC dc)
{
    ActionBand a;
    ZeroMemory(&a, sizeof(a));
    if (!g_wiz || !g_actionLabel[0])
        return a;
    // *** THE BUTTON IS ON THE SCREEN WHOSE ENTRY CARRIES IT, AND ON NO OTHER. ***
    //
    // This asked onKind(kScreenCheck) once, and then onMachineReview() once a second
    // check screen existed. Both were the same mistake at different sizes: they put
    // the flow's ONE offer on a screen chosen by the renderer rather than on the
    // screen the offer is about. A button naming one subject beside a screen about
    // a different one is association by position, which is the thing the labels
    // were written to make impossible.
    //
    // What has not changed is the second half of the gate: gone once the work has
    // started, and gone once it has finished. An offer about the machine stops being
    // an offer the moment this program is the thing changing the machine.
    if (!actionLabelOf(thisScreen()) || g_working || g_finished)
        return a;

    a.shown = true;
    SIZE sz;
    ZeroMemory(&sz, sizeof(sz));
    SelectObject(dc, g_fBody);
    GetTextExtentPoint32W(dc, g_actionLabel, (int)wcslen(g_actionLabel), &sz);
    a.needW = sz.cx + S(kActionPadW);
    a.w     = a.needW;
    if (a.w < S(kActionMinW))
        a.w = S(kActionMinW);
    if (a.w > S(kActionMaxW))
        a.w = S(kActionMaxW);
    // After "Check again" WHEN IT IS ON THIS SCREEN. This reads the table rather than
    // the button window, unlike footNote() below, because it is the thing that decides
    // where the buttons go: a layout that asked the windows where they are would be
    // reading back its own answer from the last time it ran. See recheckStandsHere() -
    // this used to ask whether the FLOW had a re-check, which is a different question
    // and answered wrongly on the first screen to carry an offer without one.
    a.x = leftBandStart();
    return a;
}

// One place that writes g_overrideLabel, exactly like setActionLabel().
static void setOverrideLabel(const wchar_t* label)
{
    if (!label || !label[0]) {
        g_overrideLabel[0] = 0;
        return;
    }
    wcsncpy(g_overrideLabel, label, 127);
    g_overrideLabel[127] = 0;
}

// ---------------------------------------------------------------------------
// The named door's geometry, and it is a function for the reason actionBand() is:
// ONE piece of arithmetic used by the layout, by the note's box and by the
// measurement in installer/verify.
//
// IT SITS AFTER WHATEVER IS ALREADY IN THAT BAND, asked of actionBand() rather than
// recomputed, so a screen that carried both an offer and a door would put them side
// by side instead of on top of one another. No screen in either flow does today - the
// binding screen has "Check again" and this - and the arithmetic is written for the
// case anyway, because a layout that is only correct for the current table is a
// layout that breaks in the task that changes the table.
// ---------------------------------------------------------------------------
static ActionBand overrideBand(HDC dc)
{
    ActionBand a;
    ZeroMemory(&a, sizeof(a));
    if (!g_wiz || !g_overrideLabel[0])
        return a;
    // The same second half of the gate the offer has: gone once the work has started
    // and gone once it has finished. A door out of a check is not a door out of an
    // install.
    if (!overrideLabelOf(thisScreen()) || g_working || g_finished)
        return a;

    a.shown = true;
    SIZE sz;
    ZeroMemory(&sz, sizeof(sz));
    SelectObject(dc, g_fBody);
    GetTextExtentPoint32W(dc, g_overrideLabel, (int)wcslen(g_overrideLabel), &sz);
    a.needW = sz.cx + S(kActionPadW);
    a.w     = a.needW;
    if (a.w < S(kActionMinW))
        a.w = S(kActionMinW);
    if (a.w > S(kOverrideMaxW))
        a.w = S(kOverrideMaxW);

    ActionBand before = actionBand(dc);
    if (before.shown)
        a.x = before.x + before.w + S(kActionGap);
    else
        a.x = leftBandStart();
    return a;
}

static FootNote footNote(HDC dc, int cw, int ch)
{
    FootNote f;
    ZeroMemory(&f, sizeof(f));
    if (onKind(kScreenWork) && g_working)
        f.text = g_wiz->cannotCancelNote;
    // *** THE NOTE IS ABOUT A BUTTON THAT WILL NOT INSTALL, SO IT BELONGS ON THE
    //     SCREEN WHOSE BUTTON INSTALLS. *** This asked the KIND, and on a flow with
    // two check screens that put "nothing will be written" under a button whose
    // press turns a page - a sentence answering a question nobody on that screen
    // asked, beside the one control it is not about.
    else if (primaryActionFor(g_wiz, g_screen, g_finished) == kPrimaryStart &&
             g_wiz->startBlockedNote)
        f.text = g_wiz->startBlockedNote;
    if (!f.text)
        return f;

    int footY = ch - S(kFootH);
    int left  = S(kMargin);
    // The buttons are real windows, so where they end is asked of THEM rather than
    // recomputed from the page index and the wizard - two answers to one question is
    // how a painter and a layout come to disagree. Both of page 2's left hand
    // buttons are consulted and the note starts after whichever reaches furthest,
    // so adding a third one later cannot quietly paint a sentence under a control.
    HWND leftOfNote[3] = { g_recheckBtn, g_actionBtn, g_overrideBtn };
    for (int i = 0; i < 3; i++) {
        if (!leftOfNote[i] || !IsWindowVisible(leftOfNote[i]))
            continue;
        RECT br;
        GetWindowRect(leftOfNote[i], &br);
        MapWindowPoints(HWND_DESKTOP, g_frame, (POINT*)&br, 2);
        int after = (int)br.right + S(14);
        if (after > left)
            left = after;
    }
    f.box.left   = left;
    f.box.top    = footY + S(6);
    f.box.right  = cw - S(24) - 2 * S(kBtnW) - S(10) - S(14);
    f.box.bottom = ch - S(6);
    if (f.box.right < f.box.left + S(40))
        f.box.right = f.box.left + S(40);

    RECT calc = f.box;
    SelectObject(dc, g_fSmall);
    DrawTextW(dc, f.text, -1, &calc, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    f.needH = calc.bottom - calc.top;
    return f;
}

// THE PANE, CREATED IN ONE PLACE AND FILLED IN ANOTHER, and the split is the change
// this round makes.
//
// It used to be one function that created the control and poured page 2's walkthrough
// into it, once, for ever, because there was one pane and it belonged to one page.
// A pane is a property of a SCREEN now - the MIDI port's carries the provenance of
// somebody else's installer, the machine review's carries the walkthrough - so the
// control is created once (a window built from a navigation handler, or worse from a
// worker's message, is a window built at the wrong time or on the wrong thread) and
// its CONTENTS are replaced whenever the flow moves to a screen that has some.
//
// It is a function rather than a block inside runWizard() because installer/verify
// builds this window itself, control by control, in order to render it on a private
// desktop - and a harness that repeated these style bits would be asserting on its
// own copy of them. Calling this means the pane the pictures are taken of IS the pane
// the product makes.
//
// WS_VSCROLL and ES_MULTILINE and NO ES_AUTOHSCROLL are the whole guarantee: an
// EDIT without ES_AUTOHSCROLL word wraps instead of running off the right edge, and
// WS_VSCROLL makes whatever does not fit vertically reachable. Both are properties
// of the control and hold at any DPI and any window size.
//
// ES_AUTOVSCROLL is deliberately NOT here, unlike the other two panes. That bit
// scrolls to the caret as text is APPENDED, which is what a running log wants; this
// pane is written once per screen and has to open at its first line, because its
// first line is the caption that says what it is.
static void buildPane(HWND page, const Wizard* wiz)
{
    if (!flowHasPane(wiz))
        return;
    g_review = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                               WS_CHILD | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE |
                               ES_READONLY,
                               0, 0, 10, 10, page, (HMENU)IDC_REVIEWPANE, g_inst, 0);
    if (!g_review)
        return;                       // the flow simply has no pane; see layout()
    SendMessageW(g_review, EM_SETLIMITTEXT, 0, 0);
}

// This screen's words, into the one control. Called from setScreen(), so the pane a
// screen shows is the pane its own entry describes and nothing carries over from the
// screen before it.
//
// IT SCROLLS BACK TO THE TOP EVERY TIME, and that is not tidiness: the first line is
// the caption that says what the pane is, so a pane that opened where the last reader
// left it would be a block of text with no statement of what it belongs to.
static void loadPane(const Screen* s)
{
    const wchar_t* text = paneTextOf(s);
    if (!g_review || !text)
        return;
    const wchar_t* cap  = s->paneCaption ? s->paneCaption : L"";
    SIZE_T         need = wcslen(cap) + wcslen(text) + 8;
    wchar_t*       buf  = (wchar_t*)HeapAlloc(GetProcessHeap(), 0,
                                              need * sizeof(wchar_t));
    if (buf) {
        buf[0] = 0;
        if (cap[0]) {
            wcscpy(buf, cap);
            wcscat(buf, L"\r\n\r\n");
        }
        wcscat(buf, text);
        SetWindowTextW(g_review, buf);
        HeapFree(GetProcessHeap(), 0, buf);
    } else {
        SetWindowTextW(g_review, text);
    }
    SendMessageW(g_review, EM_SETSEL, 0, 0);
    SendMessageW(g_review, EM_SCROLLCARET, 0, 0);
}

// ---------------------------------------------------------------------------
// THE TWO CHOICE CONTROLS' WORDS **AND STATE**, TAKEN FROM THE TABLE AND PUT INTO THE
// WINDOW. ONE DIRECTION ONLY, AND THAT IS THE WHOLE DESIGN OF THIS CONTROL.
//
// The offer and the door only need their WORDS copied. This one has a value as well,
// and a value is where a control and a table drift apart: Screen::choiceSelected is
// written by buildScreens(), and rebuildScreens() runs that builder again over the
// machine every re-check reads. A window that held the selection itself would have it
// silently overwritten by every press of "Check again", or would go on showing a
// selection the table no longer agrees with - the two-answers-to-one-question shape
// this round removed from the action label, on a control where the wrong answer means
// installing for the wrong mixer.
//
// So the flow owns the choice and the window shows it. The press does not set the
// state either: it tells the flow, the flow restates the table, and this copies the
// table back down. That is why it is called from three places and not two - a move to
// another screen, a finished re-check, and a press.
//
// BM_SETCHECK AND NOT A SIMULATED CLICK, deliberately: setting the check does not send
// BN_CLICKED, so telling the window what the flow already knows cannot loop back into
// choose() and record the same choice a second time.
static void syncChoiceButtons(void)
{
    const Screen* s   = thisScreen();
    bool          has = screenHasChoice(s);
    for (int i = 0; i < 2; i++) {
        if (!g_modelBtn[i])
            continue;
        SetWindowTextW(g_modelBtn[i], has ? s->choiceLabels[i] : L"");
        SendMessageW(g_modelBtn[i], BM_SETCHECK,
                     (WPARAM)((has && s->choiceSelected == i) ? BST_CHECKED
                                                             : BST_UNCHECKED),
                     0);
    }
}

// The words on the contextual button, taken from the screen the window is on and put
// into the control. One place, called from the two moments the answer can change: a
// move to another screen, and a re-check that has just rebuilt the table from a
// machine it read again.
static void syncActionButton(void)
{
    const wchar_t* label = actionLabelOf(thisScreen());
    setActionLabel(label);
    if (g_actionBtn)
        SetWindowTextW(g_actionBtn, g_actionLabel);
    // ...and the door beside it, from the same moments and for the same reason. It
    // is here and not in a second function because the two answers change together:
    // both are properties of the screen the window has just moved to, or of a table
    // a re-check has just rebuilt.
    const wchar_t* door = overrideLabelOf(thisScreen());
    setOverrideLabel(door);
    if (g_overrideBtn)
        SetWindowTextW(g_overrideBtn, g_overrideLabel);

    // ...and the two choice controls, from the same two moments. See
    // syncChoiceButtons() just below for why their STATE is copied here too and never
    // held by the window.
    syncChoiceButtons();
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

    // "Check again", at the far left of the same band. Only on page 2, only while
    // nothing is in flight, and only when the flow supplied a re-check at all.
    if (g_recheckBtn) {
        SetWindowPos(g_recheckBtn, 0, S(24), by, S(kRecheckW), btnH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        // ON EVERY CHECK SCREEN, and that is deliberate rather than left over: the
        // re-check reads the whole machine and, since this round, restates the whole
        // table from what it found - so on any screen that carries a measured row,
        // pressing it really does change what that screen says. The day it stops
        // doing that on some screen is the day it has to stop being offered there,
        // because a button that measures and shows nothing is worse than no button.
        //
        // The condition itself moved into recheckStandsHere(), which is now also what
        // the two bands beside it ask so that they cannot place themselves after a
        // button this screen does not show.
        showOnly(g_recheckBtn, recheckStandsHere());
    }

    // And the contextual action beside it. Placed BEFORE footNote() is ever asked
    // anything, because the note's box is measured from where these two end.
    if (g_actionBtn) {
        HDC        bdc = GetDC(g_frame);
        ActionBand ab  = actionBand(bdc);
        ReleaseDC(g_frame, bdc);
        if (ab.shown)
            SetWindowPos(g_actionBtn, 0, ab.x, by, ab.w, btnH,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        showOnly(g_actionBtn, ab.shown);
        // DISABLED WHENEVER Install IS *AND WHAT IT DOES IS A WRITE*, which is the
        // correction of 2026-08-01. startBlockedNote is set by /preview, whose written
        // promise is that nothing is written and nothing is registered, and by a machine
        // this installer has already refused. A page that has said it will not act must
        // not have a button left that ACTS ON THIS MACHINE - but `Open the Zadig page`
        // does not: it opens a web page, exactly like the painted address beside it,
        // which /preview leaves live. Greying one and not the other was gating by
        // category. See previewRefusesAction().
        //
        // The button stays VISIBLE either way, grey or not, so that /preview shows the
        // window the user would really get - which is the only thing /preview is for.
        EnableWindow(g_actionBtn,
                     (!previewRefusesAction(thisScreen()) && !g_rechecking) ? TRUE
                                                                            : FALSE);
    }

    // And the named door, beside whichever of those two is there. Placed BEFORE
    // footNote() is asked anything, like the offer, because the note's box starts
    // after whichever control reaches furthest.
    if (g_overrideBtn) {
        HDC        odc = GetDC(g_frame);
        ActionBand ob  = overrideBand(odc);
        ReleaseDC(g_frame, odc);
        if (ob.shown)
            SetWindowPos(g_overrideBtn, 0, ob.x, by, ob.w, btnH,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        showOnly(g_overrideBtn, ob.shown);
        // *** GREY UNDER /preview, LIKE EVERY OTHER BUTTON THAT DOES SOMETHING. ***
        // Taking this door writes a line into the log and changes the exit code this
        // program finishes with, and /preview's written promise is that nothing
        // happens. It stays VISIBLE and grey rather than vanishing, exactly like
        // Install and like the offer, because showing the window the user would
        // really get is the only thing /preview is for.
        //
        // ...and dead while a worker reads the machine, for the reason the primary
        // is: what this door claims about is being re-measured underneath it, and a
        // press that lands mid re-check would record a claim about a reading that is
        // already being replaced.
        EnableWindow(g_overrideBtn,
                     (!g_wiz->startBlockedNote && !g_rechecking) ? TRUE : FALSE);
    }

    // ---------------------------------------------------------------------
    // Inside the page. TWO PASSES, AND THE SCROLL BAR IS WHY.
    //
    // A vertical scroll bar takes about 17 logical pixels off the page's client
    // width. This function used to measure that width ONCE, at the top, and decide
    // about the scroll bar at the BOTTOM - so on any layout that changed the
    // decision, every child had already been sized for the width the page used to
    // have. The visible consequence was the log pane on page 3 being 17 pixels
    // narrower than its page for the rest of the run, whenever page 2 had been
    // scrolling when Install was pressed.
    //
    // NOT REASONED - MEASURED, in the committed captures. At 144 DPI page 2 already
    // overflowed, so page-3-progress-144dpi.png has always had the narrow pane; at 96
    // DPI page 2 reported 404 against 410 and just fitted, so that picture had the
    // wide one. The two rows this round adds push 96 DPI to 463 and the defect
    // appeared at both DPIs, which is what made it findable.
    //
    // Pass 0 measures and decides the bar; pass 1 measures again at the width that
    // decision left, re-decides on that width, and only then places the children.
    // A borderline page - one that overflows only because the bar is up - can still
    // leave the two passes disagreeing by one iteration, which is a smaller and rarer
    // error than the one this replaces, and is stated here rather than hidden.
    //
    // *** AND THAT PARAGRAPH IS NOW A MEASUREMENT RATHER THAN A PREDICTION. *** It was
    // read as a candidate cause for two small overflows and was instrumented instead
    // of assumed, one run printing every pass's width and answer. What it does:
    //
    //   144 DPI, "Plug the mixer in": pass 0 measures 464 at a page 1033 wide - the
    //   bar left up by the screen BEFORE it - decides the bar is not needed, and pass
    //   1 measures 441 at 1050. 23 logical pixels, exactly one re-wrapped line.
    //
    // So the loop is real and it is this size. What it is NOT is the cause of anything
    // that overflowed: the MIDI port screen reported 349 at 1033 AND at 1050, the
    // binding screen 1072 at both, so their deficits do not move with the width at
    // all. Both were the trailing bottom margin - see kPageBottomPad - and the two
    // that remain are content. The one thing that DID change here is that a screen
    // which now fits leaves the bar down for the screen after it, so pass 0 and pass 1
    // start from the same width far more often than they used to.
    for (int pass = 0; pass < 2; pass++) {
        RECT p;
        GetClientRect(g_page, &p);
        int pw = p.right, ph = p.bottom;
        int m  = S(kMargin);

        HDC dc = GetDC(g_page);
        int chrome = 0;
        if (onKind(kScreenWork))
            chrome = renderWorkChrome(dc, pw, true);
        else if (onKind(kScreenDone))
            chrome = renderDoneChrome(dc, pw, true);

        g_contentH = 0;
        if (onKind(kScreenInfo))
            g_contentH = renderInfoScreen(dc, pw, ph, true);
        else if (onKind(kScreenCheck))
            g_contentH = renderCheckScreen(dc, pw, ph, true);
        ReleaseDC(g_page, dc);

        // This screen's text pane, and what it does to the scrolling half of the
        // page. Worked out here, before the scroll info below is written, because
        // the strip the painted content scrolls in is the page MINUS the pane - and
        // a scroll range computed against the whole page would promise the user rows
        // that are behind the pane and cannot be brought out from under it.
        //
        // *** ASKED OF THE ENTRY, WHICH IS THE CHANGE THIS ROUND MAKES. *** This read
        // onMachineReview() and a Wizard field, so the pane was the machine review's
        // and no other screen could have one however loudly its entry asked: setting
        // Screen::paneText did nothing at all and said nothing about it. A screen
        // whose paneText is null still takes the whole page, which is what makes its
        // strip the whole page and is asserted per screen by installer/verify.
        int paneH = 0;
        if (g_review && paneTextOf(thisScreen())) {
            paneH = ph * kReviewPaneShare / 100;
            if (paneH < S(kReviewPaneMinH))
                paneH = S(kReviewPaneMinH);
            if (ph - paneH - S(kReviewPaneGap) < S(kReviewRowsMinH))
                paneH = ph - S(kReviewPaneGap) - S(kReviewRowsMinH);
            // A window short enough to reach here has nothing good to offer either
            // half. The pane is dropped rather than shrunk to a sliver, and the page
            // goes back to being what it was before this round - which is a page
            // that still works.
            if (paneH < S(48))
                paneH = 0;
        }
        g_viewH = paneH > 0 ? ph - paneH - S(kReviewPaneGap) : ph;
        if (g_viewH < 1)
            g_viewH = 1;

        // Only the review and welcome pages can grow past the window, and only they
        // scroll. Decided BEFORE the children are placed, which is the whole point of
        // the two passes.
        // *** THE RANGE AND THE FIT ARE TWO QUESTIONS AND THEY GET TWO NUMBERS. ***
        // g_contentH is what has to be visible and is what decides whether there is a
        // bar at all; g_scrollH is how far the bar may travel, and it carries the
        // bottom margin so that the last line of a scrolling page is not jammed
        // against the foot band. See kPageBottomPad.
        //
        // *** WHAT THIS COSTS ON A PAGE THAT FITS, SAID PLAINLY BECAUSE THE FIRST
        //     DRAFT OF THIS COMMENT GOT IT WRONG. *** It said "on a page that fits they
        // are equal and the margin is simply the paper that was already there". That is
        // true of renderWelcome(), which ANCHORS its fine print at h - block -
        // S(kPageBottomPad) and therefore really does leave that paper. It is NOT true
        // of renderSubject() or renderReview(), whose content ends wherever it ends: a
        // screen that paints to exactly g_viewH now gets no bar and keeps only the last
        // block's own trailing space - S(8) after a bullet, S(14) after a row - between
        // its last line and the bottom edge of the page.
        //
        // That is a real narrowing and it is accepted rather than hidden. Before this
        // round the margin was inside g_contentH, so such a screen raised a bar and got
        // its 20 pixels - at the price of a scroll bar on top of a scrolling pane, which
        // is what this whole round removed. 2b-midi sat on exactly that boundary at 96
        // DPI (221 into 221) until the correction moved it to 201 into 221, so the case
        // is not hypothetical; it is one sentence away. Nothing asserts a minimum gap
        // between the last ink row and the page's edge, and if a screen ever lands back
        // on the boundary that is the check to add - the harness already computes the ink
        // box that would measure it.
        g_scrollH = g_contentH;
        if (g_scrollH > g_viewH)
            g_scrollH += S(kPageBottomPad);

        SCROLLINFO si;
        ZeroMemory(&si, sizeof(si));
        si.cbSize = sizeof(si);
        si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin   = 0;
        si.nMax   = (g_scrollH > 0 ? g_scrollH - 1 : 0);
        si.nPage  = (UINT)g_viewH;
        if (g_scrollY > g_scrollH - g_viewH)
            g_scrollY = g_scrollH - g_viewH;
        if (g_scrollY < 0)
            g_scrollY = 0;
        si.nPos = g_scrollY;
        SetScrollInfo(g_page, SB_VERT, &si, TRUE);
        ShowScrollBar(g_page, SB_VERT, g_contentH > g_viewH);

        if (pass == 0)
            continue;

        if (paneH > 0)
            SetWindowPos(g_review, 0, m, ph - paneH, pw - 2 * m, paneH,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        showOnly(g_review, paneH > 0);

        // ---------------------------------------------------------------
        // *** THE TWO CHOICE CONTROLS, PLACED IN THE BOX THE MEASURING PASS ABOVE
        //     RESERVED FOR THEM, AND NOT IN A BOX COMPUTED AGAIN HERE. ***
        //
        // The g_contentH assignment at the top of this pass has already run
        // renderSubject(), which is what wrote g_choiceRow - so this reads the answer
        // rather than recomputing it. Two pieces of arithmetic for one rectangle is
        // how the Zadig picture's recorded box came to describe something nothing
        // drew, and this one is worse: the box would be what installer\verify
        // measures and the controls would be where the user clicks.
        //
        // THE POSITIONS ARE PAGE COORDINATES AND ARE NOT OFFSET BY g_scrollY, and
        // that is correct rather than forgotten: scrollTo() does not pass
        // SW_SCROLLCHILDREN, so no child of this page moves when it scrolls. On this
        // screen the page does not scroll at all - Rule 1's ratchet holds it at
        // deficit zero, see kChoiceLeadH - so the question does not arise; a screen
        // that started scrolling under these controls would fail that check first.
        // ---------------------------------------------------------------
        {
            bool onChoice = screenHasChoice(thisScreen()) && !g_working && !g_finished;
            for (int i = 0; i < 2; i++) {
                if (!g_modelBtn[i])
                    continue;
                if (onChoice && g_choiceShown)
                    SetWindowPos(g_modelBtn[i], 0, g_choiceRow[i].left,
                                 g_choiceRow[i].top,
                                 g_choiceRow[i].right - g_choiceRow[i].left,
                                 g_choiceRow[i].bottom - g_choiceRow[i].top,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                showOnly(g_modelBtn[i], onChoice && g_choiceShown);
                // Dead under /preview and on a machine this installer has already
                // refused, for the reason every other control that records something
                // is: choose() writes a line into the log and changes what this run
                // will say it was told. /preview's written promise is that nothing
                // happens. Visible and grey rather than absent, exactly like Install,
                // because showing the window the user would really get is the only
                // thing /preview is for.
                EnableWindow(g_modelBtn[i],
                             (!g_wiz->startBlockedNote && !g_rechecking) ? TRUE : FALSE);
            }
        }

        if (onKind(kScreenWork)) {
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
        if (onKind(kScreenDone)) {
            int sy = chrome + S(6);
            int sh = ph - sy - S(16);
            if (sh < S(40))
                sh = S(40);
            SetWindowPos(g_summary, 0, m, sy, pw - 2 * m, sh,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
}

// ---------------------------------------------------------------------------
// *** WHETHER THE SECONDARY BUTTON GOES BACK OR CLOSES, IN ONE PLACE, BECAUSE ITS
//     WORD AND ITS DEED ARE THE SAME QUESTION. ***
//
// They were two expressions in two functions and they DISAGREED, and the round that
// added a second kScreenInfo screen is the round that made them disagree visibly.
// refreshButtons() wrote "Back" only inside `if (onKind(kScreenCheck))`, so on the
// "Get Zadig" screen the control read Cancel - while IDC_SECONDARY's own condition,
// which asks nothing about the kind, really did turn the page back. A button reading
// Cancel that goes back is the same class as the button reading Install that turned a
// page, and it was found by looking at a foot band capture in the round that added
// the screen.
//
// It is the ACTION's condition that survived, because that is the one describing what
// the press does; the label now follows it. Work and done are excluded here rather
// than by trusting that the button is hidden or disabled on them: a navigation rule
// that depends on a control's visibility breaks the day somebody makes it visible.
// ---------------------------------------------------------------------------
static bool secondaryGoesBack()
{
    return !onKind(kScreenWork) && !onKind(kScreenDone) && g_screen > 0;
}

static void refreshButtons()
{
    // *** THE PRIMARY BUTTON'S WORDS COME FROM THE TABLE, THROUGH THE SAME PURE
    //     FUNCTION THE HARNESS ASKS. *** They used to come from a switch with one
    // word - the flow's, held in a Wizard field of its own - spliced into two of
    // its four arms, and that is the reason "the opening's button says Install and
    // installs nothing" survived six rounds: there was no expression anything
    // outside this file could evaluate. Now there is one, and it is the one the
    // window uses. That field is gone as of this round: the word lives on the
    // screen that carries it and nowhere else.
    //
    // *** THE CONTROL REALLY GETS THIS STRING ON EVERY SCREEN, INCLUDING THE ONES
    //     WHERE IT IS THEN HIDDEN. *** SetWindowTextW() below runs before any
    // showOnly(), so primaryLabelFor(screen) == GetWindowTextW(g_primary) holds for
    // every screen in the table and the harness asserts exactly that. A label
    // written only on the screens where the button happens to be visible would make
    // the table describe something the window does not do.
    const wchar_t* primary   = primaryLabelFor(g_wiz, g_screen, g_finished);
    // The word comes from what the press DOES - see secondaryGoesBack(), which is
    // the expression IDC_SECONDARY itself asks.
    const wchar_t* secondary = secondaryGoesBack() ? L"Back" : L"Cancel";
    bool showPrimary   = true;
    bool showSecondary = true;
    bool enablePrimary = true;

    if (onKind(kScreenCheck)) {
        // *** DISABLED BY WHAT THE PRESS DOES, NOT BY THE KIND OF SCREEN IT IS ON.
        //     *** startBlockedNote is /preview's promise that nothing will be
        // written, and a machine this installer has refused. Asked of the kind, it
        // greyed the primary on EVERY check screen - so on a flow with two of them
        // /preview could not be walked past screen 1, and the one thing /preview
        // exists for is looking at the window. Turning a page writes nothing.
        //
        // *** AND nextAllowed() GETS ITS FIRST CONSUMER HERE. ***
        //
        // It has been declared, defined and called by nothing since the table was
        // introduced, because nothing set blockNextWhenUnmet and wiring it earlier
        // would have been unobservable. The binding screen is the one entry in either
        // flow that sets it, and the reason is consequence: without the MIDI port the
        // audio works and only the controls die, so that screen does not block;
        // without the WinUSB binding NOTHING works, because it is how the driver
        // reaches the hardware.
        //
        // *** THE BUTTON IS GREY AND STILL THERE, WHICH IS THIS PROGRAM'S OWN RULE
        //     AND NOT A NEW ONE. *** A greyed button beside an explanation answers
        // the question; a missing button leaves the user guessing. What explains it
        // on this screen is not a foot note but the screen itself - its row NAMES the
        // control that undoes the refusal, and the door stands in the same band saying
        // what to press if the binding is missing only in the reading.
        //
        // *** BOTH HALVES ARE ONE CALL NOW, AND IT IS THE CALL THE OTHER TWO SITES
        //     MAKE. *** They were two `if`s here, the same two in the handler for a
        // finished re-check, and only the /preview one in the recovery path taken when
        // CreateThread fails - so that third site handed Next back enabled on this very
        // screen. See primaryEnabledFor() and the block above it.
        enablePrimary = primaryEnabledFor(g_wiz, g_screen, g_finished);
    } else if (onKind(kScreenWork)) {
        // The start button is gone rather than greyed: it has already been
        // pressed, and offering it again in any state invites a second press.
        showPrimary   = false;
        enablePrimary = false;
    } else if (onKind(kScreenDone)) {
        showSecondary = false;
    }

    // *** AND DEAD WHILE A WORKER IS READING THE MACHINE, ON WHATEVER SCREEN THIS IS.
    //     *** startReviewWorker() greys this button and then the user presses Back and
    // Next. That path runs setScreen(), setScreen() runs this function, and this
    // function used to hand the button straight back - so on the review screen the
    // primary read `Install`, was enabled, and swallowed the press in silence, because
    // startWork()'s `if (... || g_rechecking || ...) return;` refused it. Nothing was
    // corrupted; the second lock held. What was wrong is the button: an enabled button
    // that swallows a press is a button whose words and whose deed disagree, which is
    // the one class this whole redesign exists to remove.
    //
    // IT IS THE LOCK RE-APPLIED AND NOT A NEW RULE. The press that started the worker
    // already disabled this control on every screen; all this does is survive
    // navigation. layout() has done exactly this for the contextual action button
    // since that button existed - `!g_wiz->startBlockedNote && !g_rechecking` - and
    // the primary was the one that did not.
    //
    // IT IS NOT PERMANENT: WM_BCD_RDONE clears g_rechecking and re-enables the button
    // itself, deliberately without calling this function, so that the focus stays on
    // the button the user just pressed. installer/verify asserts both halves - dead
    // during, alive after - on a screen whose press only turns a page.
    if (g_rechecking)
        enablePrimary = false;

    SetWindowTextW(g_primary, primary);
    SetWindowTextW(g_secondary, secondary);
    EnableWindow(g_primary, enablePrimary ? TRUE : FALSE);
    showOnly(g_primary, showPrimary);
    // On the work page the Cancel button stays visible and disabled, next to a
    // sentence that says why. Hiding it would be tidier and would leave the user
    // guessing whether cancelling is possible; a greyed button beside "this
    // cannot be stopped" answers the question without promising anything.
    EnableWindow(g_secondary, onKind(kScreenWork) ? FALSE : TRUE);
    showOnly(g_secondary, showSecondary);
    if (enablePrimary)
        SetFocus(g_primary);
}

static void setScreen(int screen)
{
    g_screen  = screen;
    g_scrollY = 0;
    // Everything below asks onKind(), which reads g_screen - so the assignment above
    // has to come first. Which controls a screen carries is a property of its KIND
    // and not of its position, which is what lets a flow put its work screen sixth
    // instead of third without a line of this file changing.
    showOnly(g_bar,     onKind(kScreenWork));
    showOnly(g_log,     onKind(kScreenWork));
    showOnly(g_summary, onKind(kScreenDone));
    // This screen's own pane text, before the layout that sizes it: the control is
    // one control and what is in it is a property of the screen. Hidden here and
    // SHOWN by layout(), which is the only thing that knows whether there is room
    // for it. Two places deciding "is the pane visible" is how a painter and a
    // layout come to disagree - the same trap footNote() names.
    if (paneTextOf(thisScreen()))
        loadPane(thisScreen());
    else
        showOnly(g_review, false);
    // ...and this screen's own offer, for the same reason: the button is one button
    // and its words belong to the entry the window is on.
    syncActionButton();
    refreshButtons();
    layout();
    InvalidateRect(g_page, 0, TRUE);
    InvalidateRect(g_frame, 0, FALSE);
}

// Go to the one screen of a kind. Every flow this program builds has exactly one
// work screen and exactly one done screen, and installer/verify asserts it by COUNT
// over both flows - so the lookup cannot miss, and that assertion is the thing this
// function's correctness actually hangs on.
//
// *** WHAT A MISS WOULD DO, WRITTEN OUT BECAUSE IT IS NOT WHAT IT SOUNDS LIKE. ***
// Skipping setScreen() skips refreshButtons() and layout() with it, so a miss does
// not leave a readable window on its last screen - it leaves a STALE one, still
// wearing the controls of a screen the program has already left:
//
//   from WM_BCD_DONE: a full progress bar, no summary, and not one enabled control
//   in the window - the title bar X is the only way out of it.
//
//   from startWork(): the confirmation page, still offering a primary button whose
//   action the worker has already begun taking.
//
// Neither is blank and neither is readable. That is why the count above is asserted
// rather than this path being relied on: this is the least bad thing to do with a
// table that should not exist, not a fallback anybody should be content to reach.
static void goToKind(ScreenKind kind)
{
    int i = screenOfKind(g_wiz, kind);
    if (i >= 0)
        setScreen(i);
}

// ---------------------------------------------------------------------------
// The worker thread and the three things it is allowed to say
// ---------------------------------------------------------------------------
struct StepMsg {
    RowState state;
    wchar_t  detail[kRowText];
};

// The same shape for a review row, plus the title: a re-check may change what a row
// is called - "Bound and connected right now" against "not connected right now" -
// and a message that carried only the state would leave the old words under the new
// mark, which is the one thing worse than not re-checking at all.
struct RowMsg {
    RowState state;
    wchar_t  title[kRowTitle];
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

// postStep()'s twin for the review, line for line: the message is copied into a heap
// block that the WINDOW thread owns and frees, never a pointer into the worker's
// stack. The worker's Wizard is a local of its own, so nothing here reads or writes
// g_wiz - that belongs to the other thread.
void postReviewRow(int index, RowState state, const wchar_t* title,
                   const wchar_t* detail)
{
    if (!g_frame || index < 0 || index >= kMaxRows)
        return;
    RowMsg* m = (RowMsg*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(RowMsg));
    if (!m)
        return;
    m->state = state;
    if (title) {
        wcsncpy(m->title, title, kRowTitle - 1);
        m->title[kRowTitle - 1] = 0;
    }
    if (detail) {
        wcsncpy(m->detail, detail, kRowText - 1);
        m->detail[kRowText - 1] = 0;
    }
    if (!PostMessageW(g_frame, WM_BCD_ROW, (WPARAM)index, (LPARAM)m))
        HeapFree(GetProcessHeap(), 0, m);
}

void postReviewDone(int newCount)
{
    if (!g_frame)
        return;
    PostMessageW(g_frame, WM_BCD_RDONE, (WPARAM)newCount, 0);
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

// The re-check, on a thread of its own.
//
// IT CANNOT BE CALLED FROM frameProc, AND THAT IS STRUCTURAL. This file has no
// declaration of the function it runs: it has g_wiz->recheck, a pointer setup.cpp
// filled in, and the only place that pointer is dereferenced is the line below -
// which is already on the worker thread by the time it runs. There is nothing for a
// message handler to call by name, so "the re-check is not on the window thread" is a
// property of the code's shape rather than a rule somebody has to remember.
static DWORD WINAPI recheckProc(LPVOID)
{
    // Two pointers, one thread slot. Which one is called is the ONLY difference
    // between a re-check and an action, because an action ends by measuring - see
    // the block on Screen::action in gui.h.
    //
    // *** THE ACTION IS READ OUT OF A SLOT THE PRESS FILLED, NOT OFF THE SCREEN THE
    //     WINDOW IS ON NOW. *** The offer belongs to a screen, and WHAT THIS WINDOW
    // DISABLES WHILE A WORKER RUNS IS, EXACTLY: "Check again", the contextual action,
    // and the primary button - the last of them on every screen, including the ones
    // where it says `Next` and writes nothing. What stays alive is CANCEL, and with it
    // Back, because a page that traps somebody while it re-reads the registry is worse
    // than one that lets them leave.
    //
    // (That list used to read "the only control this window disables is the one that
    // WRITES". It was false when it was written - startReviewWorker() greys the
    // primary unconditionally - and the round that measured it found the other half of
    // the same sentence: setScreen() handed the button back on the way past, so it was
    // enabled and inert at once. Both are fixed; the comment now says what the code
    // does.)
    //
    // So the user really can turn to another screen while winget is open, and
    // thisScreen() really would answer with a different entry by the time this line
    // ran. Taking the pointer at the press is what makes "the button ran ITS OWN
    // screen's action" true for the whole life of the worker rather than only at the
    // moment of the click.
    //
    // THREE POINTERS NOW AND NOT TWO. The painted address opens itself through this
    // same slot, and it is tried first because it is the only one of the three that
    // carries an argument: the URL the renderer painted, captured at the click for the
    // same reason the pointer is. It is cleared wherever g_actionFn is cleared, so
    // "which one is in flight" has exactly one answer.
    int count = 0;
    if (g_addressFn)
        count = g_addressFn(g_wiz->user, g_addressUrl);
    else if (g_runningAction)
        count = g_actionFn ? g_actionFn(g_wiz->user) : 0;
    else
        count = g_wiz->recheck ? g_wiz->recheck(g_wiz->user) : 0;
    // Posted from HERE rather than left to the worker, so that a re-check which
    // returns early - out of memory, a registry read that failed - still re-enables
    // the button. A page whose only route back is a function the worker might not
    // reach is a page that can be dead for the rest of the run.
    postReviewDone(count);
    return 0;
}

// ---------------------------------------------------------------------------
// *** NOTHING WAS READ AND NOTHING WAS CHANGED: THE BUTTONS GO BACK TO WHAT THE
//     SCREEN SAYS THEY SHOULD BE. ***
//
// Reached when CreateThread fails in startReviewWorker() below. It is a function of
// its own for two reasons, and neither is tidiness.
//
// THE FIRST IS THAT IT GOT THE PRIMARY WRONG. It handed Next back on
// `g_wiz->startBlockedNote` alone - the /preview half of the rule and not the other
// half - and startBlockedNote is null on a normal run. So on the ONE screen in the
// flow that refuses to be left, a failed CreateThread left Next ENABLED, and
// IDC_PRIMARY's own guard then swallowed the press. A button that is enabled and
// swallows the press is exactly the defect this redesign was started to remove, and
// it was in the third of three places that ask the same question. It asks
// primaryEnabledFor() now, like the other two.
//
// THE SECOND IS THAT NOTHING COULD REACH IT. It was a block inside an `if
// (!CreateThread(...))`, and a harness cannot make CreateThread fail. As a function
// it is the real recovery path, callable, so installer/verify drives THIS code on
// the real window and reads the real button back - rather than asserting a rule and
// hoping the site obeys it, which is what let the defect stand.
//
// The other three controls keep the /preview-only test on purpose: none of them is
// gated by nextAllowed(). "Check again" is how a blocked screen becomes unblocked
// and must never be greyed by the block; the offer and the door are refused by
// /preview and by nothing else. Only the primary carries both halves.
// ---------------------------------------------------------------------------
static void giveTheButtonsBack(void)
{
    g_rechecking    = false;
    g_runningAction = false;
    g_actionFn      = 0;
    // ...and the address opener, at every one of the three places g_actionFn is
    // cleared. A pointer left behind here would make the NEXT plain re-check run the
    // address's opener instead, because recheckProc() tries this slot first.
    g_addressFn     = 0;
    g_addressUrl    = 0;
    g_recheckTid    = 0;
    if (g_recheckBtn)
        EnableWindow(g_recheckBtn, TRUE);
    // By consequence and not by category, through the same expression layout() and the
    // press use - see previewRefusesAction(). The DOOR below keeps the blanket rule on
    // purpose: taking it writes a line into the log and changes the exit code this
    // program finishes with, so it is a write in the sense the mode means.
    if (g_actionBtn)
        EnableWindow(g_actionBtn, previewRefusesAction(thisScreen()) ? FALSE : TRUE);
    if (g_overrideBtn)
        EnableWindow(g_overrideBtn, g_wiz->startBlockedNote ? FALSE : TRUE);
    EnableWindow(g_primary,
                 primaryEnabledFor(g_wiz, g_screen, g_finished) ? TRUE : FALSE);
}

// ONE STARTER FOR ALL THREE, because all three end in the same place: rows posted by
// index into the page the Install button acts on. Two starters would be two copies of
// the locking, and the lock is the whole point - see the block on Wizard::action. That
// argument did not weaken when a third job arrived, it got stronger: the painted
// address is the first thing in this program that can start a worker WITHOUT a control
// being pressed, and giving it a starter of its own would have given it a copy of the
// locking that nothing forces to stay in step with this one.
enum WorkerJob {
    kJobRecheck = 0,   // the flow's own re-check, the only one /preview allows
    kJobAction  = 1,   // the screen's contextual button
    kJobAddress = 2    // a click on the painted download address
};

static void startReviewWorker(WorkerJob job)
{
    if (g_rechecking || g_working || g_finished)
        return;
    if (job == kJobAddress) {
        // *** THE SAME SECOND LOCK THE BUTTON HAS, AND ASKED THROUGH THE SAME
        //     EXPRESSION THE HIT TEST AND THE CURSOR ASK. *** So "there is a link
        // here", "the pointer changes here" and "there is something to run here" cannot
        // answer differently.
        //
        // *** AND THERE IS NO startBlockedNote TERM HERE, WHICH IS THE ONE DIFFERENCE
        //     FROM THE BUTTON BELOW AND IS DELIBERATE. *** /preview's written promise is
        // that nothing is written and nothing is registered. Asking a browser to open a
        // public address does neither, so it is outside that promise rather than an
        // exception to it - and on the MIDI port screen the two controls then do exactly
        // what the mode is for: the button INSTALLS and is refused, the address OPENS A
        // PAGE and is not. Blocking both would be blocking by appearance instead of by
        // consequence. See Screen::addressOpen in gui.h for the whole ruling, including
        // why silence would have been the worse answer here: a painted region has
        // nothing to grey and nothing to put an explanation beside.
        const Screen* s = thisScreen();
        if (!addressOpenOf(s))
            return;
        g_addressFn  = s->addressOpen;
        // THE POINTER THE RENDERER PAINTED, not a lookup done later. The opener is one
        // function shared by both screens, so what makes it open THIS screen's page is
        // this line and nothing else - which is why installer/verify reads the argument
        // back and compares it with Screen::addressUrl.
        g_addressUrl = s->addressUrl;
    } else if (job == kJobAction) {
        // The second lock on a run that has already said it will not act. The
        // button is greyed too; this is the one that holds if a keyboard or an
        // accessibility tool sends the command anyway.
        //
        // AND IT IS ASKED OF THE SCREEN THE PRESS CAME FROM. actionLabelOf() is the
        // same expression actionBand() uses to decide whether the button is on the
        // screen at all, so "there is a button here" and "there is something to run
        // here" cannot answer differently.
        //
        // AND BY CONSEQUENCE, through the same expression the greying uses: a button
        // that only opens a page is not what /preview is protecting anybody from. See
        // previewRefusesAction().
        const Screen* s = thisScreen();
        if (!actionLabelOf(s) || previewRefusesAction(s))
            return;
        g_actionFn = s->action;
    } else if (!g_wiz->recheck) {
        return;
    }
    // ONE PRESS, ONE SNAPSHOT. The buttons go dead first, so that a second press
    // cannot start a second reader whose rows would interleave with the first's and
    // leave the page showing half of each.
    g_rechecking    = true;
    // TRUE FOR THE ADDRESS TOO, and that is not a shortcut: the flag means "the thing
    // in flight is the screen's own and it ends by measuring", which is exactly what a
    // click on the address does. Everything downstream that reads it - the greying, the
    // done handler, the recovery - wants the same answer for both.
    g_runningAction = (job != kJobRecheck);
    if (g_recheckBtn)
        EnableWindow(g_recheckBtn, FALSE);
    if (g_actionBtn)
        EnableWindow(g_actionBtn, FALSE);
    // ...and the door, because what it claims about is being re-measured underneath
    // it: a press that landed mid re-check would record a claim about a reading that
    // is already being replaced.
    if (g_overrideBtn)
        EnableWindow(g_overrideBtn, FALSE);
    // And so does Install, for the reason in startWork(): what it acts on is being
    // rewritten while this runs.
    EnableWindow(g_primary, FALSE);
    g_recheckTid = 0;
    g_recheck    = CreateThread(0, 0, recheckProc, 0, 0, &g_recheckTid);
    if (!g_recheck) {
        // Nothing was read and nothing was changed. Say so and give the buttons back
        // rather than leaving them grey with no explanation.
        sayFail(L"could not start the %s (%s) - the page still shows what was "
                L"measured when it opened",
                (job == kJobAddress) ? L"page"
                                     : ((job == kJobAction) ? L"action" : L"re-check"),
                winErrText(GetLastError()));
        giveTheButtonsBack();
    }
}

static void startRecheck()
{
    startReviewWorker(kJobRecheck);
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
    // g_rechecking is in this list because the re-check REWRITES the snapshot the
    // install acts on. Two threads in that structure at once is a read of a state
    // being rewritten, and the install would be acting on half of each. The Install
    // button is greyed while a re-check runs, so this is the second lock and not the
    // only one.
    if (g_working || g_finished || g_rechecking || g_wiz->startBlockedNote)
        return;
    g_working = true;
    goToKind(kScreenWork);
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
        goToKind(kScreenDone);
    }
}

// ---------------------------------------------------------------------------
// The page window
// ---------------------------------------------------------------------------
static void scrollTo(HWND wnd, int pos)
{
    // g_viewH and not the client height: on a screen with a pane the bottom of the
    // page is that pane, which does not move. Scrolling past the last row into the
    // space behind a pane is a scroll bar that promises something it cannot deliver.
    //
    // g_scrollH and not g_contentH, since the round that separated them: the travel
    // includes the bottom margin, the fit does not. See kPageBottomPad.
    int maxY = g_scrollH - (g_viewH > 0 ? g_viewH : 1);
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

// ---------------------------------------------------------------------------
// *** IS THIS POINT ON THE PAINTED ADDRESS, AND IS THERE ANYTHING FOR A CLICK THERE TO
//     OPEN? ***
//
// ONE EXPRESSION, ASKED BY BOTH ARMS THAT NEED IT - the click and the cursor. Two
// copies of a hit test is how a pointer comes to turn into a hand over a region that
// then refuses the press, which is the "enabled and inert" defect this redesign already
// found once in a real button.
//
// *** THE POINT COMES IN IN CLIENT COORDINATES AND THE RECTANGLE IS IN PAGE ONES. ***
// pageProc()'s WM_PAINT draws with the viewport origin moved by -g_scrollY, so every
// rectangle the renderer records is unscrolled. A mouse message carries where the
// pointer is in the window. Adding g_scrollY is the whole of the conversion, and it is
// done HERE rather than at the two call sites for the same reason the test itself is.
// (Neither screen that carries an address scrolls its page today - Rule 1 holds both at
// deficit zero - so g_scrollY is zero on both. It is added anyway: "this screen happens
// not to scroll" is a property of two other screens' arithmetic, and a hit test that
// depends on it is a hit test that breaks the day a line is added somewhere else.)
//
// It reads no machine state and starts nothing. It is a question about a rectangle.
// ---------------------------------------------------------------------------
static bool addressLinkAt(int clientX, int clientY)
{
    if (!g_addrLink || !addressOpenOf(thisScreen()))
        return false;
    POINT p;
    p.x = clientX;
    p.y = clientY + g_scrollY;
    return PtInRect(&g_addrLinkBox, p) != 0;
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
        if (onKind(kScreenInfo))
            renderInfoScreen(mem, c.right, c.bottom, false);
        else if (onKind(kScreenCheck))
            renderCheckScreen(mem, c.right, c.bottom, false);
        else if (onKind(kScreenWork))
            renderWorkChrome(mem, c.right, false);
        else if (onKind(kScreenDone))
            renderDoneChrome(mem, c.right, false);
        RestoreDC(mem, save);
        BitBlt(dc, 0, 0, c.right, c.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(wnd, &ps);
        return 0;
    }

    case WM_VSCROLL: {
        int pos = g_scrollY;
        switch (LOWORD(wp)) {
        case SB_LINEUP:   pos -= S(24); break;
        case SB_LINEDOWN: pos += S(24); break;
        case SB_PAGEUP:   pos -= g_viewH; break;
        case SB_PAGEDOWN: pos += g_viewH; break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK: pos = HIWORD(wp); break;
        }
        scrollTo(wnd, pos);
        return 0;
    }

    case WM_MOUSEWHEEL:
        scrollTo(wnd, g_scrollY - (GET_WHEEL_DELTA_WPARAM(wp) * S(40)) / WHEEL_DELTA);
        return 0;

    // ---------------------------------------------------------------------
    // *** THE PAINTED DOWNLOAD ADDRESS, PRESSED. IT IS THE FIRST THING IN THIS PROGRAM
    //     THAT ACTS WITHOUT BEING A CONTROL. ***
    //
    // The owner asked for it in those words after walking all nine screens: the Zadig
    // screen has a button at the bottom that goes to the site, and "o link azul no
    // comeco da pagina deveria ter link tambem". Both screens that paint an address get
    // it, not only the one he named - fixing one and leaving the other is this project's
    // signature defect, and it has been counted fifteen times.
    //
    // *** THE BUTTON STAYS, AND THAT IS A REQUIREMENT AND NOT A LEFTOVER. *** A painted
    // region is invisible to a screen reader and unreachable from the keyboard. See
    // Screen::addressOpen in gui.h: this is an addition, and if it ever became the only
    // way to the page it would have taken the address away from exactly the people who
    // most need it stated.
    //
    // *** IT IS HANDLED HERE AND NOT IN frameProc() FOR THE SAME REASON THE CHOICE IS:
    //     THE ADDRESS IS PAINTED ON THE PAGE. *** A mouse message goes to the window
    // under the pointer, so a handler written in the other window procedure would
    // compile, pass review, and never once run.
    //
    // *** AND IT OBEYS THE ONE RULE OF THIS FILE RATHER THAN BENDING IT. *** The head of
    // gui.h requires that everything reading or writing a control runs inside
    // frameProc() or pageProc(), on the main thread, because an install on the window
    // thread would grey the window out. This arm runs there and does the only two things
    // a window-thread handler may: it asks a rectangle a question, and it hands the work
    // to the worker channel that already exists. Opening a browser IS work -
    // openPageInBrowser() ends in launchUnelevated() or ShellExecuteW - so it goes
    // through startReviewWorker() exactly like the button beside it, and inherits its
    // lock, its greying and its refusal under /preview.
    //
    // NO WM_LBUTTONUP AND NO CAPTURE. Down-only is what a painted address wants: there
    // is no pressed state to draw, nothing to cancel by dragging off, and a press-release
    // pair would be two rectangles to keep in step for no gain. The click is idempotent
    // in the only way that matters, because the worker lock refuses the second one.
    // ---------------------------------------------------------------------
    case WM_LBUTTONDOWN:
        if (addressLinkAt((int)(short)LOWORD(lp), (int)(short)HIWORD(lp))) {
            startReviewWorker(kJobAddress);
            return 0;
        }
        break;

    // *** AND THE ONLY THING THAT TELLS ANYBODY THE REGION IS THERE. *** The address is
    // not underlined and its colour did not change: the owner had already read it as a
    // link before it was one - he called it "o link azul" - and altering two screens he
    // had just judged good would be spending his approval to say something he already
    // believed. The hand pointer says it at the moment somebody is about to try, and it
    // costs no pixel, which is why not one capture moves for this round. It is also why
    // installer/verify has to drive these two arms with real messages: a picture cannot
    // photograph a cursor.
    //
    // *** TWO ARMS AND NOT ONE, AND THE REASON IS THAT NEITHER ONE ALONE WORKS. ***
    // WM_SETCURSOR carries the hit-test code and the message that provoked it and NEVER
    // the position, so it has to be told where the pointer is; WM_MOUSEMOVE carries the
    // position but arrives AFTER the WM_SETCURSOR for the same movement, so a cursor set
    // only there would be undone by the next WM_SETCURSOR and flicker the whole way
    // across the address. So the move records the point and sets the cursor, and
    // WM_SETCURSOR answers from that recorded point and returns TRUE to stop Windows
    // putting the class arrow back.
    //
    // *** GetCursorPos WOULD HAVE BEEN THE OBVIOUS WAY AND IT IS THE WRONG ONE, MEASURED.
    //     *** It fails with ERROR_ACCESS_DENIED unless the calling thread's desktop is
    // the INPUT desktop, and installer/verify renders these pages on a private desktop
    // of its own. An arm written that way would be dead in the harness and alive in the
    // product - a branch nothing can test, which is how this program came by six
    // comments describing machinery that was never there. The point comes out of the
    // message, so the harness drives exactly the code the user's mouse drives.
    case WM_MOUSEMOVE:
        g_lastMoveX = (int)(short)LOWORD(lp);
        g_lastMoveY = (int)(short)HIWORD(lp);
        if (g_handCursor && addressLinkAt(g_lastMoveX, g_lastMoveY))
            SetCursor(g_handCursor);
        break;

    case WM_SETCURSOR:
        if ((HWND)wp == wnd && LOWORD(lp) == HTCLIENT && g_handCursor &&
            addressLinkAt(g_lastMoveX, g_lastMoveY)) {
            SetCursor(g_handCursor);
            return TRUE;
        }
        break;

    // ---------------------------------------------------------------------
    // *** THE CHOICE. IT IS HANDLED HERE AND NOT IN frameProc() BECAUSE THE TWO
    //     CONTROLS ARE CHILDREN OF THE PAGE. ***
    //
    // Every other pressable control in this program belongs to the frame, so every
    // other press arrives in frameProc()'s WM_COMMAND. A radio button on the page
    // sends its notification to the page, and a handler written in the other window
    // procedure would compile, pass review, and never once run - which is the
    // "declared with no caller" shape this project has now found eleven times.
    // ---------------------------------------------------------------------
    case WM_COMMAND: {
        int id = (int)LOWORD(wp);
        if (id != IDC_MODEL0 && id != IDC_MODEL1)
            break;
        if (HIWORD(wp) != BN_CLICKED)
            return 0;
        // ASKED OF THE SCREEN THE PRESS CAME FROM, through the same expression
        // layout() and renderSubject() use, so "there is a choice here" and "there is
        // something to record here" cannot answer differently.
        //
        // *** AND THE REFUSAL PATHS PUT THE CONTROL BACK. *** BS_AUTORADIOBUTTON has
        // ALREADY moved the dot by the time this notification arrives, so a press this
        // function declines would leave the window showing a choice the flow never
        // heard. layout() disables both controls under /preview and mid re-check, so
        // no press should reach here at all; this is the second lock, and it restores
        // the state from the table rather than trusting that.
        const Screen* s = thisScreen();
        if (!screenHasChoice(s) || g_wiz->startBlockedNote || g_rechecking ||
            g_working || g_finished) {
            syncChoiceButtons();
            return 0;
        }
        // ON THE WINDOW THREAD, like the door and unlike the offer: it records a
        // selection and says it. It reads no registry and enumerates no processes,
        // which is the whole reason those two are workers.
        int ok = s->choose ? s->choose(g_wiz->user, id - IDC_MODEL0) : 0;
        if (!ok) {
            syncChoiceButtons();
            return 0;
        }
        // *** THE TABLE IS RESTATED THROUGH THE CHANNEL THAT ALREADY EXISTS, AND NOT
        //     BY WRITING choiceSelected FROM HERE. *** refreshScreens() is the flow's
        // own "say what you are now, on the window thread" - the same call the finished
        // re-check makes - and buildScreens() is a pure function of the run, so running
        // it again is running it once with the choice the press just recorded. Writing
        // the field from this side would be a second author of one value, which is the
        // drift the block over syncChoiceButtons() exists to prevent.
        if (g_wiz->refreshScreens)
            g_wiz->refreshScreens(g_wiz, g_wiz->user);
        syncActionButton();
        layout();
        InvalidateRect(wnd, 0, TRUE);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    // *** AND THE BUTTON FLAVOUR, WHICH THE PAGE NEEDED THE MOMENT IT HAD A BUTTON ON
    //     IT. *** The two panes were the only controls this page had ever carried, and
    // a radio button asks its parent for WM_CTLCOLORBTN. Without this arm the pair
    // would draw their labels on the system's button background - a grey rectangle in
    // the middle of a white page, at every DPI - which is the one kind of defect a
    // capture WOULD have shown, except that a capture cannot see a child control.
    case WM_CTLCOLORBTN:
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
    // makes the greyed Cancel honest. Wrapped, and centred in its box by measuring
    // it - DT_VCENTER only works with DT_SINGLELINE, which is the thing that was
    // cutting it.
    FootNote fn = footNote(dc, cw, ch);
    if (fn.text) {
        RECT n   = fn.box;
        int  boxH = n.bottom - n.top;
        if (fn.needH < boxH)
            n.top += (boxH - fn.needH) / 2;
        SelectObject(dc, g_fSmall);
        SetTextColor(dc, CLR_DIM);
        DrawTextW(dc, fn.text, -1, &n, DT_WORDBREAK | DT_NOPREFIX);
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
    // The SAME question runWizard() asked when it decoded them the first time, and it
    // is a union over the flow and the table - see flowNeedsPhoto(). A DPI change that
    // asked a narrower question than the first decode would drop a screen's picture on
    // the way to a bigger monitor and nowhere else, which is the worst possible place
    // for that class of defect to live.
    if (flowNeedsPhoto(g_wiz))
        decodePhoto(S(kPhotoW));
    // Decoded again at the new size for the same reason the photograph is: WIC
    // resamples at decode time, and a bitmap stretched afterwards by AlphaBlend is
    // where a screenshot's text turns to porridge.
    if (flowNeedsZadigShot(g_wiz))
        decodeZadigShot(S(kZadigShotW));
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
            // *** THE CHAIN OF onKind() TESTS THAT USED TO BE HERE IS NOW A CALL. ***
            // It decided what the button did, and it decided it in a place nothing
            // outside this file could evaluate - while the button's WORDS came from
            // primaryLabelFor(), which anything can. A rule made of two halves that
            // are not readable from the same place is how a button came to say
            // Install and install nothing. Both halves are readable now, and
            // installer/verify checks them against each other.
            switch (primaryActionFor(g_wiz, g_screen, g_finished)) {
            case kPrimaryClose:
                DestroyWindow(wnd);
                break;
            case kPrimaryAdvance:
                // BOUNDED, and the bound is not theoretical. g_screen + 1 was walked
                // to unchecked, and an Info screen that is LAST would walk it to
                // screenCount(): every onKind() then answers false, the page paints
                // nothing, every control hides, and the primary reads "Close" and
                // does nothing when it is pressed. No flow reaches that today - the
                // setup's only Info screen is entry 0 with three behind it, and
                // installer/verify asserts for both flows that the last screen is
                // the summary - but the wizard gains screens in the tasks after this
                // one and this is one ordering mistake away from being reachable.
                // Stopping on the last screen leaves a page that is still drawn and
                // still readable, which is the one thing walking off the end cannot
                // offer.
                // *** AND THE SECOND LOCK ON A SCREEN THAT REFUSES TO BE LEFT. ***
                // refreshButtons() greys this control on the same expression, and
                // this is the one that holds if a keyboard default-push or an
                // accessibility tool sends the command anyway - the same pairing
                // startReviewWorker() has with the offer's grey. A press that got
                // past the grey and turned the page would make the block a
                // suggestion.
                if (!nextAllowed(g_wiz, g_screen))
                    break;
                if (g_screen + 1 < screenCount(g_wiz))
                    setScreen(g_screen + 1);
                break;
            case kPrimaryStart:
                startWork();
                break;
            case kPrimaryNone:
                // The work screen, where the button is hidden. Reached only by a
                // keyboard default-push at a moment the control is not there, and
                // doing nothing is the whole of the right answer: startWork() would
                // be a second install and DestroyWindow() would be a close in the
                // middle of one.
                break;
            }
            return 0;
        case IDC_SECONDARY:
            // Back to the screen before this one. Work and done are excluded here
            // rather than by trusting that the button is disabled and hidden on
            // them: that is a property of two other lines of this file, and a
            // navigation rule that depends on a control's visibility is a rule that
            // breaks the day somebody makes the control visible.
            if (secondaryGoesBack())
                setScreen(g_screen - 1);
            else
                SendMessageW(wnd, WM_CLOSE, 0, 0);
            return 0;
        case IDC_RECHECK:
            startRecheck();
            return 0;
        case IDC_ACTION:
            startReviewWorker(kJobAction);
            return 0;
        case IDC_OVERRIDE: {
            // *** THE NAMED DOOR. *** See Screen::override in gui.h for the whole of
            // what makes it a door and not a second Next.
            //
            // ASKED OF THE SCREEN THE PRESS CAME FROM, through the same expression
            // overrideBand() and layout() use, so "there is a door here" and "there
            // is something to run here" cannot answer differently. That is also the
            // second lock on the "unmet only" rule: on a bound machine this returns
            // null and the press does nothing, whatever a keyboard sent.
            const Screen* s = thisScreen();
            if (!overrideLabelOf(s) || g_wiz->startBlockedNote || g_rechecking ||
                g_working || g_finished)
                return 0;
            // ON THE WINDOW THREAD, and that is a statement about what this does
            // rather than a shortcut: it writes one line and sets one bool. It reads
            // no registry and enumerates no processes, which is the whole reason the
            // re-check and the offer are workers.
            int ok = s->override ? s->override(g_wiz->user) : 0;
            // *** THE DOOR OPENS; IT DOES NOT UNBLOCK Next. *** Screen::satisfied
            // stays what the machine actually said, so nextAllowed() keeps answering
            // false and the primary stays grey. Advancing from here is the door
            // BEING the way past, which is what makes "reachable only from the unmet
            // state" survive the user pressing Back: the wall is still there, and so
            // is the door.
            if (ok && g_screen + 1 < screenCount(g_wiz))
                setScreen(g_screen + 1);
            return 0;
        }
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

    case WM_BCD_ROW: {
        RowMsg* m = (RowMsg*)lp;
        int     i = (int)wp;
        if (m) {
            if (i >= 0 && i < g_wiz->reviewCount && i < kMaxRows) {
                g_wiz->review[i].state = m->state;
                if (m->title[0]) {
                    wcsncpy(g_wiz->review[i].title, m->title, kRowTitle - 1);
                    g_wiz->review[i].title[kRowTitle - 1] = 0;
                }
                if (m->detail[0]) {
                    wcsncpy(g_wiz->review[i].detail, m->detail, kRowText - 1);
                    g_wiz->review[i].detail[kRowText - 1] = 0;
                }
            }
            HeapFree(GetProcessHeap(), 0, m);
        }
        // NOT relaid out here, unlike WM_BCD_STEP. A review is rebuilt whole, so
        // laying out per row would move the page under the reader's eye once for each
        // check. WM_BCD_RDONE does it once, when all of them have arrived.
        return 0;
    }

    case WM_BCD_RDONE: {
        int n = (int)wp;
        if (n > 0 && n <= kMaxRows)
            g_wiz->reviewCount = n;
        g_rechecking    = false;
        g_runningAction = false;
        g_actionFn      = 0;
        // The second of the three places the slot is emptied. See giveTheButtonsBack().
        g_addressFn     = 0;
        g_addressUrl    = 0;
        g_deviceChanged = false;   // the page now shows the machine as it is again
        if (g_recheck) {
            // The thread has already posted this message and has nothing left to do
            // but return, so this is a handshake and not a wait.
            WaitForSingleObject(g_recheck, 10000);
            CloseHandle(g_recheck);
            g_recheck = 0;
        }
        // *** AND THE REST OF THE TABLE HEARS THE SAME NEWS. *** The rows on page 2
        // came back one at a time while the worker ran; a screen that carries its
        // own row has no such channel, so the flow is asked to restate them here -
        // on THIS thread, after the handshake above, with the worker finished and
        // its MachineState written. See Wizard::refreshScreens. Before layout(),
        // because the screen the window is on may be one of the screens that just
        // changed height.
        if (g_wiz->refreshScreens)
            g_wiz->refreshScreens(g_wiz, g_wiz->user);
        // ...and the offer with it, because the offer is a column of that table. A
        // re-check that found the outstanding work already done leaves the entry
        // with no label, and a screen with no label has no button - so a page
        // cannot go on offering
        // work that is already done. This used to arrive as a separate message
        // carrying one string from the worker; one fact with two channels is how a
        // button comes to say something the table does not.
        syncActionButton();
        layout();
        if (g_recheckBtn)
            EnableWindow(g_recheckBtn, TRUE);
        // Through previewRefusesAction(), like the other three askers, so a worker
        // finishing cannot hand a screen a different answer from the one layout() gave
        // it a moment earlier.
        if (g_actionBtn)
            EnableWindow(g_actionBtn, previewRefusesAction(thisScreen()) ? FALSE : TRUE);
        // Enabled AGAIN and not by refreshButtons(), which would also move the focus
        // to Install - away from the button the user just pressed and is likely to
        // press again. Blocked only where the press would WRITE, for the reason on
        // the same test in refreshButtons(): a re-check on a screen whose button
        // turns a page must not leave that page with no way forward.
        //
        // *** AND IT STAYS DEAD ON A SCREEN THAT REFUSES TO BE LEFT. *** This line
        // handed the button back unconditionally, and on the binding screen that
        // would have undone the block with the first press of "Check again" on a
        // machine that is still not bound - the grey restored by refreshButtons()
        // removed here, by the one path that deliberately does not call it. It is
        // the same expression, asked in the one other place that enables this
        // control.
        EnableWindow(g_primary,
                     primaryEnabledFor(g_wiz, g_screen, g_finished) ? TRUE : FALSE);
        InvalidateRect(g_page, 0, TRUE);
        InvalidateRect(wnd, 0, FALSE);
        return 0;
    }

    // *** A DEVICE ARRIVED OR LEFT, AND NOTHING IS RE-READ HERE ON PURPOSE. ***
    //
    // The state shown has to be the state acted on. A page that refreshed itself
    // would change under the hand of the person reading it - the row they were half
    // way through would become a different row, and the snapshot the Install button
    // acts on would not be the one they agreed to. So this sets a flag and asks. The
    // user decides when the page is allowed to change, by pressing the button.
    //
    // It is also the cheap answer: gatherMachineState() reads the registry and walks
    // the process list, and Windows sends these in bursts - one plug of a composite
    // device produces several - so a handler that re-read would re-read four times.
    case WM_DEVICECHANGE:
        if (wp == DBT_DEVICEARRIVAL || wp == DBT_DEVICEREMOVECOMPLETE) {
            g_deviceChanged = true;
            if (onKind(kScreenCheck)) {
                layout();
                InvalidateRect(g_page, 0, TRUE);
            }
        }
        return TRUE;

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
        goToKind(kScreenDone);
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

    // The pointer for the painted address. A shared system cursor, so it is never
    // destroyed; and it is loaded here rather than at each WM_SETCURSOR because that
    // message arrives on every mouse move over the page. A null answer is not a failure
    // to open the window - the address still opens when it is clicked, and the arm that
    // uses this tests it - so init() does not fail on it.
    g_handCursor = LoadCursorW(0, BCD_CURSOR_HAND);

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
    // Entry 0, whatever it is. This used to read hasWelcome and pick between two
    // constants; a flow with no opening now simply has no entry 0 of that kind, and
    // the window has one less thing to know about the flow it is showing.
    g_screen   = 0;

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
    // Created only when there is something to run: an uninstaller's page 2 is a plan,
    // not a measurement, so it has nothing to check again. A button that did nothing
    // would be worse than no button.
    if (wiz->recheck)
        g_recheckBtn = CreateWindowExW(0, L"BUTTON", kRecheckLabel,
                                       WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                                       0, 0, 10, 10, frame, (HMENU)IDC_RECHECK,
                                       g_inst, 0);
    // The contextual action. Created whenever ANY screen in the flow supplies one,
    // EVEN IF there is nothing to offer at this moment: a re-check can turn an offer
    // on as well as off, and a button that had to be created when the user reached
    // the screen - or worse, from a worker's message - would be a window built at
    // the wrong time or on the wrong thread. setScreen() is what puts words in it.
    g_actionLabel[0] = 0;
    if (flowHasAction(wiz))
        g_actionBtn = CreateWindowExW(0, L"BUTTON", g_actionLabel,
                                      WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                                      0, 0, 10, 10, frame, (HMENU)IDC_ACTION,
                                      g_inst, 0);
    // ...and the named door, on the same terms and for a sharper version of the same
    // reason: whether the door is OFFERED depends on a registry read that a re-check
    // can change in either direction, so its existence must not.
    g_overrideLabel[0] = 0;
    if (flowHasOverride(wiz))
        g_overrideBtn = CreateWindowExW(0, L"BUTTON", g_overrideLabel,
                                        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                                        0, 0, 10, 10, frame, (HMENU)IDC_OVERRIDE,
                                        g_inst, 0);
    // ...and the two choice controls, on the same terms as the three above: created
    // once, before the first screen is shown, whenever ANY entry offers a choice.
    //
    // *** CHILDREN OF THE PAGE AND NOT OF THE FRAME, WHICH IS THE ONE STRUCTURAL
    //     DIFFERENCE. *** Every other pressable control here belongs to the frame,
    //     because the frame's foot band never moves. These belong to the page, because
    //     a radio button is the subject of its screen and not an action beside it. See
    //     kChoiceLeadH for what makes that safe and for the check that keeps it safe.
    //
    // WS_GROUP ON THE FIRST AND NOT THE SECOND is what makes the pair ONE group:
    // BS_AUTORADIOBUTTON clears its siblings up to the next control with WS_GROUP, so
    // without it the two would be independent and both could be on at once - a window
    // saying this machine is two different mixers. Only the first takes WS_TABSTOP,
    // which is the standard radio group shape: Tab reaches the group, the arrow keys
    // move inside it.
    //
    // BS_MULTILINE because one of the two labels is a sentence, and a radio button
    // without it puts a sentence on one line and clips it. The height reserved for each
    // is measured wrapped, so the two agree.
    //
    // Empty text at creation, filled by syncChoiceButtons() when a screen is shown, for
    // the reason the action button is: the words belong to an entry, and the control
    // exists before any entry has been reached.
    if (flowHasChoice(wiz)) {
        for (int i = 0; i < 2; i++) {
            DWORD extra = (i == 0) ? (WS_GROUP | WS_TABSTOP) : 0;
            // THROUGH UINT_PTR, and not because a cast looks tidier: the id here is
            // COMPUTED rather than a literal, so on a 64 bit build the compiler cannot
            // fold it and (HMENU)(int) is C4312 - "conversion to a type of greater
            // size" - which /WX makes an error. Every other CreateWindowExW in this
            // file passes a literal macro and never meets it.
            g_modelBtn[i] = CreateWindowExW(0, L"BUTTON", L"",
                                            WS_CHILD | BS_AUTORADIOBUTTON |
                                            BS_MULTILINE | extra,
                                            0, 0, 10, 10, g_page,
                                            (HMENU)(UINT_PTR)(IDC_MODEL0 + i),
                                            g_inst, 0);
        }
    }
    if (!g_page || !g_primary || !g_secondary || !g_log || !g_summary) {
        DestroyWindow(frame);
        g_frame = 0;
        releaseEarly();
        return wiz->cancelExitCode;
    }
    SendMessageW(g_log, EM_SETLIMITTEXT, 0, 0);
    SendMessageW(g_summary, EM_SETLIMITTEXT, 0, 0);
    buildPane(g_page, wiz);
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

    // *** ASKED OF THE FLOW **AND** OF THE TABLE. *** A screen that sets
    // Screen::showDevicePhoto on a flow whose Wizard::showDevicePhoto is false used to
    // reach a renderer with nothing decoded and draw nothing, silently. See
    // flowNeedsPhoto() for the whole of it.
    if (flowNeedsPhoto(wiz) && !decodePhoto(S(kPhotoW))) {
        // Not a reason to refuse to install. The page simply has no picture and
        // the words move left.
        //
        // Only the FLOW's flag is cleared, because it is the only one a failed decode
        // can speak for: both renderers already test the bitmap itself, so a screen
        // that asked for a photograph and did not get one draws none either way.
        wiz->showDevicePhoto = false;
    }

    // And the Zadig screenshot, which needs no flag of its own to be turned off: a
    // failed decode leaves g_shot null and both renderers draw the caption on their
    // own. The caption is deliberately NOT cleared with it - it says what to match in
    // Zadig's window, and that is true with or without a picture of the window.
    if (flowNeedsZadigShot(wiz))
        decodeZadigShot(S(kZadigShotW));

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

    setScreen(g_screen);

    // Ask Windows to say when a device interface arrives or goes. ALL interface
    // classes, because the two things page 2 is about are reached through two
    // different ones - the mixer's WinUSB interface, and whatever interface the
    // MIDI port's own driver publishes - and filtering on a guid we would have to
    // name is how a nudge comes to work for one of the two prerequisites only.
    //
    // It is a nudge and nothing else, so a failure to register is not worth a word to
    // the user: the button is still there and still does the whole job.
    HDEVNOTIFY notify = 0;
    if (wiz->recheck) {
        DEV_BROADCAST_DEVICEINTERFACE_W filter;
        ZeroMemory(&filter, sizeof(filter));
        filter.dbcc_size       = sizeof(filter);
        filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        notify = RegisterDeviceNotificationW(frame, &filter,
                                             DEVICE_NOTIFY_WINDOW_HANDLE |
                                             DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);
    }

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
    // The re-check reads and never writes, so closing the window on top of one is
    // safe - but the thread still refers to g_wiz, which is the caller's, so it is
    // waited for before this function returns and lets that go out of scope.
    if (g_recheck) {
        WaitForSingleObject(g_recheck, 30000);
        CloseHandle(g_recheck);
        g_recheck = 0;
    }
    g_rechecking    = false;
    g_runningAction = false;
    // The third and last, on the way out of runWizard(). g_actionFn is not cleared here
    // and never was, and this is not being changed to match: the two above are the ones
    // recheckProc() can still read, and this line's whole job is that a SECOND run of
    // this function does not start believing a worker is in flight.
    g_addressFn     = 0;
    g_addressUrl    = 0;
    if (notify)
        UnregisterDeviceNotification(notify);

    // The sink and the hook are cleared before the window handles go, so that a
    // late say*() from anywhere cannot post to a window that is gone.
    setLineSink(0);
    setAskHook(0);
    g_frame      = 0;
    g_page       = 0;
    g_recheckBtn = 0;
    g_actionBtn  = 0;
    g_overrideBtn = 0;
    g_review     = 0;
    // The two page level controls, cleared here with the rest. They are DESTROYED with
    // the frame, like every other control - DestroyWindow() takes the whole tree - so
    // this only drops the handles, and it matters for the same reason the others do:
    // installer\verify builds and tears down this window many times in one process, and
    // a stale HWND is a SetWindowPos into a window that no longer exists.
    g_modelBtn[0] = 0;
    g_modelBtn[1] = 0;
    g_choiceShown = false;
    ZeroMemory(&g_choiceBox, sizeof(g_choiceBox));
    ZeroMemory(g_choiceRow, sizeof(g_choiceRow));
    g_actionLabel[0]   = 0;
    g_overrideLabel[0] = 0;
    g_shotDrawn        = false;
    ZeroMemory(&g_shotBox, sizeof(g_shotBox));
    releasePhoto();
    releaseZadigShot();
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

    // *** THE TWO ROWS THAT USED TO STAND HERE ARE GONE, AND THEY WERE THE ONLY TWO
    //     ON THIS PAGE THAT WERE STATED TWICE. ***
    //
    // The MIDI port's third party dependency was row 3 and the WinUSB binding was
    // row 4. Each of them has had a screen of its own since the MIDI port and the
    // binding got theirs - the same
    // reading, from the same MachineState, written by describeMidiPort() and
    // describeBinding() - so this page was repeating two screens the reader had walked
    // through two and three screens earlier. Nothing went red for two rounds because
    // nothing compared the two statements: every check in installer/verify looked at
    // one side or the other, never at both, and both rounds shipped green. The pair of
    // checks that closes that is testInstallScreen()'s first two.
    //
    // *** THE ARGUMENT FOR KEEPING THEM, AND WHY IT EXPIRED RATHER THAN BEING
    //     OVERRULED. *** It was written here and in describeBinding(): this page is a
    // REPORT, and a report with a hole where its most important line used to be is
    // worse than a report that repeats a screen. That was true while the report was
    // the only place either fact was painted. It stopped being true when the binding
    // screen's bullets carried the procedure and the MIDI screen's pane carried the
    // provenance - and the screen this page has become says so in its own words: its
    // last bullet names both subjects as the two this installer reads and will not
    // change. The facts did not leave the program; they left this list.
    //
    // *** WHAT THAT DID TO THE INDICES, WHICH IS THE ONE PROPERTY THIS FUNCTION OWES
    //     postReviewRow(). *** Both were removed from the MIDDLE, so every row after
    // them moved down by two. That is safe for exactly the reason a row appearing at
    // the END is safe and a row appearing in the middle is not: the shape of this list
    // is fixed at COMPILE time, not per machine. A re-check runs the same
    // fillPreflightRows() this page was built from, so the two snapshots have the same
    // rows in the same order on any one machine. What has to stay true - and is
    // asserted over four account and elevation states and over both Windows versions -
    // is that the list does not depend on the MACHINE.

    // 3. Not a check of the product, but the one thing on this page that changes what
    // the installer is allowed to do, so it belongs where the checks are and not in a
    // log.
    if (!s->elevated)
        setRow(&wiz->review[n++], kRowFail, L"Administrator rights",
               L"This process is not elevated. Registering an ASIO driver writes to "
               L"HKEY_CLASSES_ROOT and to HKLM\\SOFTWARE\\ASIO, and the driver goes "
               L"into %%ProgramFiles%%. Nothing will be installed.");
    else
        setRow(&wiz->review[n++], kRowOk, L"Administrator rights",
               L"This process is elevated, so the machine wide half can be written: "
               L"the class id, HKLM\\SOFTWARE\\ASIO and %%ProgramFiles%%.");

    // 4. Which profile the per user half would go into.
    if (!s->account.checked)
        setRow(&wiz->review[n++], kRowWarn, L"Which account this is for",
               L"The account that owns the desktop could not be read, so the per user "
               L"folders are this process's own profile.");
    else if (!s->account.matched)
        setRow(&wiz->review[n++], kRowWarn, L"Which account this is for",
               L"Running as %s while the desktop belongs to %s. The driver is machine "
               L"wide and will be installed; the control service and its startup "
               L"shortcut live in a user profile and will be refused, because they "
               L"would go into the wrong one.",
               s->account.tokenAccount, s->account.shellAccount);
    else
        setRow(&wiz->review[n++], kRowOk, L"Which account this is for",
               L"%s owns the desktop as well as this process, so the per user files - "
               L"the control service and its startup shortcut - go in the right place.",
               s->account.shellAccount);

    // 5. Windows 10, and ONLY on Windows 10. NEUTRAL, and it is the last row.
    //
    // *** WHY IT IS NOT A WARNING. *** Nothing here is wrong with this machine and
    // there is nothing for the user to fix. It is a fact that opens a door this
    // program cannot walk through for them: the reason this project exists is that
    // Windows 11 24H2 stopped trusting the 2010 driver's cross signed SHA-1
    // signature, and that removal was never announced for Windows 10 - so on
    // Windows 10 the manufacturer's own package may still work, and it is less work
    // than all of this. An amber mark would be the page calling somebody's working
    // operating system a defect.
    //
    // *** AND WHY IT CLOSES THE LOOP IN THE SAME BREATH. *** The two choices are
    // mutually exclusive: installing the manufacturer's package is precisely what
    // warning 1 says never to do, because Windows then matches those INF files over
    // the WinUSB binding and both halves die at once. Naming the option without
    // naming that would be an invitation to destroy the install this page is
    // walking somebody through.
    //
    // *** WHY IT IS APPENDED RATHER THAN SLOTTED IN. *** postReviewRow() addresses a
    // row by index. Every row above is emitted on every machine, so their indices
    // are fixed; this one is not, and putting it last is what keeps its presence
    // from moving any of them. Within one run it cannot appear or disappear anyway -
    // Windows does not change its build number under a running process - but the
    // ordering costs nothing and does not depend on that being remembered.
    if (s->os.read && s->os.isWindows10)
        setRow(&wiz->review[n++], kRowNeutral, L"Windows 10, and the choice it leaves you",
               // AND IT HAS TO FIT IN kRowText. setRow() truncates at 511
               // characters and says nothing, and the first draft of this sentence
               // was cut off at "...over the WinUSB binding and t". A row that
               // stops mid word is worse here than anywhere else on the page,
               // because the half it loses is the half that says not to take the
               // option the first half offers. installer/verify now measures every
               // row against that limit.
               L"This machine reports Windows 10 (build %lu). This project exists "
               L"because Windows 11 24H2 stopped trusting the 2010 driver's cross "
               L"signed SHA-1 signature, and that removal was not announced for "
               L"Windows 10 - so the manufacturer's package may still work for you, "
               L"and it is less work than this. Nobody here has tested that. The two "
               L"are mutually exclusive: installing theirs is exactly what this "
               L"installer warns you never to do, and it leaves you with no audio "
               L"and no controls.",
               (unsigned long)s->os.build);

    // *** WHY ROWS 3 AND 4 ARE ALWAYS EMITTED, INCLUDING WHEN THERE IS NOTHING WRONG.
    //     IT IS NOT SYMMETRY, IT IS postReviewRow()'s CONTRACT. ***
    //
    // These two used to be one slot with three branches and a fourth that emitted
    // NOTHING, so this function answered with four rows on a healthy machine and five
    // otherwise, and row 4 was about elevation on one machine and about accounts on
    // another. postReviewRow() addresses a row BY INDEX, so a re-check that crossed
    // one of those boundaries would have posted its row 4 into a page whose row 4 asks
    // a different question - and the page would then be stating something false about
    // a check nobody looked at. Two permanent rows, one question each, is what makes
    // the index mean something.
    //
    // The count was NOT made stable by dropping information: both new branches say
    // more than the silence they replaced, and installer/verify measures that the four
    // account-and-elevation states still get four distinguishable answers.
    wiz->reviewCount = n;
}

}
