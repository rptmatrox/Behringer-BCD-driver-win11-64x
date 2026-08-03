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
// control runs inside frameProc() or pageProc() - the two window procedures in
// gui.cpp - on the main thread. The reason is not tidiness: an install writes
// files and registry keys, and if that ran on the window's thread Windows would
// stop repainting, grey the window out and add "(not responding)" to the title -
// which looks exactly like the thing we are trying to stop looking like.
//
// (Those are the real names. This block, and two blocks in gui.cpp, used to name a
// wizardProc() that has never existed in this repository - a rule whose subject was
// invented, which is the one kind of comment that survives review indefinitely
// because nobody can grep for a thing that is not there.)

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

// *** THE WORDS ON THE "CHECK AGAIN" BUTTON, PUBLISHED SO THAT A SENTENCE MAY NAME
//     IT AND MEAN THE SAME CONTROL. ***
//
// It was a literal inside CreateWindowExW() and nothing else in the program could
// say it. That mattered on the one screen that REFUSES TO BE LEFT: the binding
// screen greys Next when the WinUSB binding is missing, and the only labelled way
// forward painted anywhere on it was the door - "Already applied - continue
// anyway", which is the CLAIM and not the honest path. The design's own rule is a
// greyed button beside an EXPLANATION, and the explanation has to name the control
// that undoes the refusal.
//
// So describeBinding() in setup.cpp INTERPOLATES this into its sentence rather than
// spelling the words a second time, and installer/verify asserts that every reading
// which leaves Next grey carries it. Renaming the button therefore carries the row
// with it - there is no second copy to drift - and the check's failing state is the
// one that matters: a sentence rewritten so that the screen stops naming the control
// at all, which is exactly how it stood before this round.
extern const wchar_t* const kRecheckLabel;

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
// A SCREEN. One subject, one thing to read, one thing to decide.
//
// This replaces a four value page enum, and the reason is a defect that took six
// rounds to become visible: every round added something good to page 2, because
// page 2 was the only place there was, and nobody asked what the page looked like
// after all six. A table cannot accumulate that way - a new subject is a new
// entry, and an entry that carries two subjects looks wrong in the table itself.
//
// EVERY CONTENT FIELD BELOW IS READ BY THE RENDERER, AND SETTING ONE IS ENOUGH.
// The list that used to sit here said that paneText, showZadigShot and
// showDevicePhoto were declared and unread - the pane came from a Wizard field, the
// picture from another, the photograph from a third - so a screen that set any of the
// three got nothing and was told nothing. That is the shape this project has paid for
// four times: content that vanishes without a trace.
//
// So the round that gave the MIDI port a screen taught renderSubject() and layout()
// to read them, and it did it for ALL THREE rather than for the one field it needed,
// because the screen after this one puts the Zadig picture on a screen of its own and
// would otherwise hit the same wall. The pane is the interesting one: it is a real
// EDIT control, so it is layout() and not the painter that reads paneText, and the
// strip the painted content gets is the page MINUS that pane.
//
// *** AND "READ BY THE RENDERER" WAS NOT THE SAME AS "ENOUGH", WHICH IS THE HALF THAT
//     ROUND OVERSTATED AND ITS REVIEW MEASURED. *** A picture has two gates, not one:
// the renderer draws a bitmap and something else had to DECODE it. The two decode
// sites read Wizard::showDevicePhoto and Wizard::zadigCaption, so a screen that set
// its own flag on a flow whose Wizard field was null reached a renderer holding
// nothing and drew nothing - no picture and no complaint, the very defect the
// sentence above claimed was closed. It only worked because setup.cpp sets both.
// gui.cpp's flowNeedsPhoto() and flowNeedsZadigShot() are the union that closes it -
// every decode gate asks them - and installer/verify asserts the sufficiency on a
// flow that offers neither picture, which is the shape the defect took.
// ---------------------------------------------------------------------------
const int kMaxScreens = 10;

enum ScreenKind {
    kScreenInfo  = 0,  // words, and possibly a picture. Measures nothing.
    kScreenCheck = 1,  // ONE subject, measured on a worker. May act. May block Next.
    kScreenWork  = 2,  // the progress bar and the live log. Exactly one per flow.
    kScreenDone  = 3   // the summary. Exactly one per flow, and it is the last.
};

struct Screen {
    ScreenKind     kind;
    // Painted at the top of the page on every screen, once the renderer moves onto
    // this table. IT IS ALSO WHAT ENDS THE TABLE: screenCount() walks until it
    // meets an entry without one, so a screen with no title is not a screen.
    const wchar_t* title;
    const wchar_t* primaryLabel;  // what pressing the foot band button DOES

    // *** WHETHER THAT PRESS STARTS THE WORK, SAID BY THE TABLE RATHER THAN
    //     INFERRED FROM THE KIND. ***
    //
    // primaryActionFor() used to answer kPrimaryStart for EVERY kScreenCheck. That
    // was true of a flow with one check screen and false of the flow the design
    // asks for, which has five: the moment a second check screen existed the
    // program really would have had two buttons that each write to this machine,
    // and the only thing between that and a user was a harness failure. Loud is
    // not the same as impossible.
    //
    // A screen that MEASURES something is not automatically a screen that INSTALLS
    // it. Exactly one entry per flow sets this - installer/verify counts the
    // screens whose action is kPrimaryStart and asserts that count is one - and a
    // check screen that does not set it advances like any other screen.
    bool           startsTheWork;

    // *** WHICH ENTRY PAINTS THE FLOW'S REVIEW ROWS. ONE THING, SAID BY THE TABLE
    //     RATHER THAN GUESSED FROM THE KIND. ***
    //
    // It was written to carry six: the review rows, the walkthrough pane, the Zadig
    // picture, the Check again button, the contextual action and the note beside them
    // were all gated on it, and this block used to say the last of the tasks that took
    // those subjects away would DELETE the field. That prediction was wrong in both
    // halves and the round that measured it is the round that corrected this. Five of
    // the six left one at a time and each went to a home of its own -
    // Screen::paneText, Screen::showZadigShot, the KIND for Check again (a re-check
    // changes what any check screen says), Screen::actionLabel, and
    // primaryActionFor() for the note. What is left is one thing:
    //
    //   renderCheckScreen() renders the flow's Wizard::review rows through
    //   renderReview() on the entry that sets this, and the entry's own single row
    //   through renderSubject() on every other.
    //
    // *** AND THAT ONE THING IS WHY IT SURVIVES. *** renderSubject() paints ONE row -
    // Screen::row, the entry's answer about its own subject. The review is FIVE rows
    // that arrive by index through postReviewRow() while the re-check worker is still
    // running, which is what makes them fill in progressively. Deleting this field
    // would mean giving Screen an array and a worker-to-window channel to carry what
    // Wizard::review and postReviewRow() already carry, for no behaviour anybody
    // wants. A flag that names one renderer is smaller than the machinery that would
    // replace it.
    //
    // IT IS A FIELD AND NOT A KIND TEST, for the reason startsTheWork is: the moment
    // a second check screen existed, "kScreenCheck" stopped being able to mean "the
    // review page". The alternative was inferring it from bullets[0] being null, which
    // is a rule nobody would find - and which is now false as well as unfindable,
    // because the entry that sets this carries bullets of its own and renderReview()
    // paints them above the rows.
    bool           paintsMachineReview;

    // *** AND THE SAME QUESTION FOR THE OPENING, WHICH THIS ROUND HAD TO ASK. ***
    //
    // renderWelcome() paints the Wizard's welcomeLine1, welcomeBullets and the
    // credits block, and the dispatch chose it for kScreenInfo - which was true of a
    // flow whose ONLY Info screen was entry 0. "Get Zadig" is the second: it measures
    // nothing, because Zadig installs nothing and leaves no key, so there is no
    // reading a row on it could honestly paint. Under the old dispatch that screen
    // drew the opening page word for word, with its own bullets on no page at all -
    // and the deficit check caught it only because the opening's height is a number
    // this file already knew. Content that vanishes in silence is the class this
    // project has paid for five times.
    //
    // IT IS A FIELD AND NOT A POSITION TEST for the reason paintsMachineReview is:
    // "entry 0" is a rule about a table that later tasks reorder, and a flow that
    // grew a screen in front of the opening would silently move the whole welcome
    // page onto it.
    bool           paintsOpening;

    // -----------------------------------------------------------------------
    // *** THE DOWNLOAD ADDRESS, PAINTED ON THE SCREEN AND OUTSIDE THE PANE. ***
    //
    // WHY IT IS A FIELD AT ALL, and the answer is a person and not a measurement.
    // The owner ran the program on 2026-07-31 and said the download links have to
    // come OUT of the instruction box and sit somewhere obvious ABOVE the summary
    // text, and that the top of a screen has to be direct about what to do without
    // much technicality. The evidence was in his own screenshot: he had SELECTED the
    // address on that screen with the mouse, inside a pane that scrolls. Nothing in
    // installer/verify can see somebody hunting.
    //
    // WHERE IT IS DRAWN: renderSubject() paints it immediately after the title, the
    // rule and the screen's own row, and BEFORE the bullets - which is his first
    // decision, taken on mock-ups. The first thing after "what this is" and "what was
    // found" is "where to get it".
    //
    // *** IT IS BOTH TEXT AND A BUTTON, WHICH IS HIS SECOND DECISION AND HAS THREE
    //     RECORDED REASONS. *** Seeing the address lets a person check it BEFORE
    // clicking, which matters when this program is telling them to fetch a third
    // party .exe; being able to select and copy it survives a broken default browser;
    // and a button saves typing, because typing an address wrong is exactly how
    // somebody downloads the wrong program. The button is Screen::actionLabel and
    // Screen::action - the pair that already exists - and not a second mechanism.
    //
    // *** addressLead IS WHAT MAKES THIS CONDITIONAL RATHER THAN A MOVE, AND THAT IS
    //     THE PART A LATER ROUND MUST NOT SIMPLIFY AWAY. *** The address is prominent
    // when the thing is MISSING. On a machine that already has it the top says what
    // was found and where - which is the screen's own row - and says nothing about
    // downloading, because an unconditional "download here" would be instructing
    // somebody to do something they do not need. That is the same class of false
    // statement this program has had removed from it twelve times. On the owner's own
    // machine the MIDI port screen is green and its row reads "There is nothing to do
    // on this screen"; a "download it here" underneath that would have been the
    // thirteenth. So a flow decides per machine: see buildScreens() in setup.cpp,
    // which builds each screen's offer from its own MachineState reading and leaves
    // both fields null when there is nothing a download would fix.
    //
    // *** addressUrl IS THE PUBLISHED CONSTANT AND NEVER A SECOND COPY OF IT. ***
    // kZadigDownloadPage is in common.cpp, one definition, and the console, the log
    // file and the pane already print it. A literal spelled again here would be an
    // address nothing forces to agree with the one the rest of the program says -
    // the drift this project has already closed for the re-check button's words and
    // for the repository address.
    //
    // *** AND "ONE DEFINITION" IS ASSERTED BY READING THE SOURCE, NOT BY COMPARING
    //     POINTERS - THIS SENTENCE USED TO SAY THE OPPOSITE AND AN INJECTION DISPROVED
    //     IT. *** It read "installer/verify asserts POINTER identity with the published
    // constant, so a byte-identical copy fails it". A byte-identical copy was written
    // here in place of the constant and every check stayed green. The mechanism was
    // measured rather than reasoned, and it is NOT /OPT:ICF: with /OPT:NOICF the
    // literals still fold, and only /GF- un-folds them. So it is /GF's content-derived
    // COMDAT names plus ordinary duplicate-symbol elimination, which /O2 turns on - the
    // definition and the copy become ONE address in the image, and the two sides of the
    // pointer comparison are the same object. Anybody "repairing" that check with
    // /OPT:NOICF would restore a check that still cannot fail and would believe they had
    // fixed it. The pointer comparison is kept for what it CAN say - that this screen
    // carries its own address and not the other screen's - and single definition is
    // asserted by testAddressIsDefinedOnce() in installer/verify, which counts the
    // literal across the installer's seven sources as text.
    //
    // A null addressUrl is "no address block on this screen", which is what every
    // entry in both flows carries except two.
    const wchar_t* addressLead;   // the words in front of it; drawn dim
    const wchar_t* addressUrl;    // the published constant, drawn in the accent colour

    // -----------------------------------------------------------------------
    // *** AND THE PAINTED ADDRESS OPENS ITSELF WHEN IT IS CLICKED. ***
    //
    // WHY IT EXISTS, and it is a person again and not a measurement. The owner walked
    // all nine screens on 2026-07-31 and judged them, and this was the one thing he
    // asked for: "no passo do zadig tem o botao la embaixo para ir para o site, mas o
    // link azul no comeco da pagina deveria ter link tambem". He already READS the
    // accent-coloured line as a link - he called it "o link azul" - so on both screens
    // that carry one it now behaves like the thing he already believed it was.
    //
    // *** IT IS AN ADDITION AND NEVER A REPLACEMENT, WHICH IS THE PART A LATER ROUND
    //     MUST NOT SIMPLIFY AWAY. *** A painted region is invisible to a screen reader
    // and unreachable from the keyboard. Screen::actionLabel is what covers everybody
    // who does not use a mouse, and it stays on both screens exactly as it was. If this
    // ever became the only way to reach the page, the program would have taken the
    // address away from the people who need it stated most.
    //
    // *** RULE 2 GOVERNS IT, AND A PAINTED REGION THAT ACTS IS A NEW CATEGORY IN THIS
    //     PROGRAM, SO THE REASONING IS WRITTEN DOWN RATHER THAN ASSUMED. *** The design's
    // rule is that a control names the consequence of pressing it. This one is the
    // strongest form of that there is: the region's own text IS the address it opens, so
    // it cannot promise a different destination from the one it goes to - and
    // buildScreens() hands the renderer and the opener the SAME pointer, so they cannot
    // come apart. On a screen whose entry offers to install through winget, the button
    // beside the address says `Install <subject>...` and does that; the address says
    // the address and does that. Two controls, two true statements, neither one
    // covering for the other - which is exactly the gap the owner is closing, because
    // in that state the button is the installer and nothing on the screen would open
    // the page.
    //
    // *** IT TAKES THE URL AS AN ARGUMENT RATHER THAN KNOWING ONE. *** gui.cpp passes
    // Screen::addressUrl - the very pointer renderSubject() painted - so one function
    // serves both screens and there is no per-screen opener that could be pointed at the
    // other screen's page. installer/verify reads the argument back and asserts it is the
    // field the renderer drew.
    //
    // *** THE MECHANISM IS Screen::action's AND THE CONTRACT IS NOT, AND THE ONE PLACE
    //     THEY DIFFER IS THE ONE THING THIS FIELD DOES DIFFERENTLY. *** Called ONCE per
    // click, on a thread of its own, with Wizard::user; it may say*() and post*(); it
    // shares the re-check's thread slot and in-flight flag with the contextual button, so
    // a click inherits one press one snapshot and cannot start a second worker. The
    // window thread never blocks on it, because opening a browser is a process launch -
    // openPageInBrowser() ends in launchUnelevated() or ShellExecuteW - which is why this
    // is a worker like the offer and not a window-thread handler like the door.
    //
    // *** BUT IT DOES NOT END BY MEASURING, AND THAT IS A DECISION WITH A NUMBER BEHIND
    //     IT. *** Screen::action ends by measuring because an action CHANGES the machine:
    // a winget offer runs somebody else's installer, so the page after it has to be
    // read again. This one asks a browser to open a public address. Nothing it does can
    // change what was measured - the user has not even downloaded anything yet, let alone
    // installed it - and installer/verify measured what a re-check costs: 203 to 703 ms
    // on the controller's machine, spent greying four controls to repaint the same
    // numbers. So it returns 0, which recheckProc() and the WM_BCD_RDONE handler already
    // treat as "nothing was posted, leave the rows alone" - a value the slot has always
    // supported, because a null action pointer produces it.
    //
    // THAT IS ONE CONTRACT PER FIELD AND NOT TWO CONTRACTS FOR ONE FIELD, which is the
    // objection openZadigPage() records against an action that returns 0 without
    // measuring. That objection stands and Screen::action is untouched by this:
    // openZadigPage() is a Screen::action and still ends by measuring. What the two share
    // is the LAUNCH AND WHAT IS SAID ABOUT IT, which is one function in setup.cpp, so the
    // button and the address on the same screen cannot come to describe the same act
    // differently.
    //
    // *** AND IT IS NOT REFUSED BY /preview, WHICH IS THE OTHER PLACE IT PARTS FROM THE
    //     BUTTON. *** /preview's written promise is that nothing is written and nothing
    // is registered. Opening the user's browser at a public address writes nothing to
    // this machine and registers nothing, so it is OUTSIDE that promise rather than an
    // exception to it. The apparent inconsistency on the MIDI port screen is the mode
    // working correctly: the button there INSTALLS, which is a write, so /preview greys
    // it; the address OPENS A PAGE, which is not, so it stays live. A mode that blocked
    // both would be blocking by appearance instead of by consequence, which is the
    // opposite of what Rule 2 asks of this program. And the alternative was worse than
    // inconsistent: the owner's own rule is that a greyed button beside an explanation
    // answers the question while a missing one leaves the user guessing - a painted
    // address that swallows a click is neither, because there is nothing to grey and
    // nothing to explain. (Ruled by the controller, 2026-07-31.)
    //
    // Null means the painted address is text and nothing else, which is what every entry
    // in both flows carries except two. It is set and cleared with addressUrl, so the hit
    // region follows the address: no address, nothing clickable.
    int          (*addressOpen)(void* user, const wchar_t* url);

    const wchar_t* bullets[4];    // short painted lines; a null ends the list
    bool           showDevicePhoto;
    bool           showZadigShot;

    // The pane, and it is the ONLY thing on a screen that is allowed to scroll.
    // Null means the screen has no pane and its bullets are the whole of it.
    //
    // THE CAPTION IS THE PANE'S FIRST LINE rather than a painted label above it,
    // and it is a field of the SCREEN rather than of the flow for the reason
    // Wizard::startVerb was deleted for: the pane belongs to a screen now, and a
    // caption held one level up is a second place the same sentence can live.
    const wchar_t* paneCaption;
    const wchar_t* paneText;

    // --- kScreenCheck only ---
    //
    // *** THERE IS NO measure() FUNCTION POINTER HERE, AND THERE NEVER WAS A CALLER
    //     FOR ONE. *** It was declared with a comment saying each screen measures its
    // subject on a thread of its own and reports it with postScreenRow(). No such
    // function as postScreenRow() has ever existed, nothing ever read the pointer, and
    // the rows below were being written by buildScreens() and describeCable() on the
    // window thread all along - so the comment described a mechanism, a thread and a
    // contract that were all imaginary, sitting beside a field that looked like the
    // way to use them.
    //
    // HOW A SCREEN REALLY GETS ITS ROW: buildScreens() writes it from the
    // MachineState, and refreshScreens() below restates it from the MachineState the
    // re-check worker has already finished writing. Both are pure functions of that
    // state, run on the thread that paints, which is why no worker to window channel
    // is needed for them and why none was ever built. A screen whose subject needs
    // real measuring adds its describe*() beside describeCable() and gets it for free.
    //
    // *** action() IS BACK, AND THIS TIME IT HAS A CALLER. *** It was deleted with
    // measure() in the round that found both unread, and it is re-added in the round
    // that needed one: a screen's own offer belongs beside the screen it is about
    // and nowhere else. It replaces Wizard::action, which was ONE action for the
    // whole flow gated on "is this the machine review" - a rule that could only ever
    // put one button on one screen, and the screen after this one needs two on one
    // screen at once.
    //
    // The contract is the one Wizard::action had, unchanged, because the mechanism
    // underneath is unchanged: called ONCE per press, on a thread of its own, with
    // Wizard::user; it may say*() and post*(); and it ENDS BY MEASURING - it returns
    // the number of review rows it posted, exactly like recheck(), so an action IS a
    // re-check with something in front of it and inherits every guard the re-check
    // already has.
    //
    // gui.cpp is GIVEN it as a pointer rather than able to name it, for the same
    // structural reason as recheck(): this file has no declaration of the function,
    // so a message handler cannot call it on the window thread even by mistake.
    const wchar_t* actionLabel;   // NAMES ITS OWN SUBJECT, or null for no action
    int          (*action)(void* user);

    // *** WHAT /preview GATES ON, AND IT IS THE CONSEQUENCE AND NOT THE CATEGORY. ***
    //
    // /preview's written promise is that nothing is WRITTEN and nothing is REGISTERED.
    // It used to keep that promise by greying every contextual button, which is gating
    // by what a control IS rather than by what pressing it DOES - and the round that
    // made the painted address clickable made the inconsistency visible on one screen:
    // `Open the Zadig page` sat grey beside a live blue link that opens the same page.
    // Two controls, one consequence, opposite answers.
    //
    // So a screen says whether its action only opens a page. When it does, /preview
    // leaves it alone: asking a browser to open a public address writes nothing here.
    // When it does not - a winget offer runs a third party installer - it stays grey,
    // which is the whole of what the mode is for.
    //
    // *** false IS THE SAFE DEFAULT AND THAT IS WHY THE FIELD IS PHRASED THIS WAY
    //     ROUND. *** A zeroed table, and every entry in both flows that never thinks
    //     about this, answers "no" and is therefore treated as a write and refused. A
    //     field named actionWrites would have had the opposite default and would have
    //     let a screen added later run its action under /preview by saying nothing.
    //
    // IT IS NOT A PROPERTY OF THE LABEL. A screen offering a winget install can read
    // `Install <subject>...` on a machine winget can serve and `Open <subject> page` on
    // every degraded one, so buildScreens() sets this from the offer's KIND - the same
    // decision the label comes from. And a winget offer's worker refuses the winget
    // branch under /preview on its own account, so the promise does not rest on a
    // button's greyness staying in step with a ladder that is re-run at the press.
    bool           actionOnlyOpensAPage;

    bool           blockNextWhenUnmet;

    // *** THE NAMED DOOR, AND IT HAS A FUNCTION NOW BECAUSE IT HAS A CALLER. ***
    //
    // overrideLabel was declared in the round that introduced this table and had no
    // reader and no function beside it for three rounds, which is the shape this
    // project keeps finding on the wrong side of a comment. It is a pair now, exactly
    // like actionLabel and action: a screen that blocks Next offers a second control
    // whose words are its own, and pressing it runs the flow's own function.
    //
    // *** WHAT IT IS FOR, AND IT IS NOT CONVENIENCE. *** Only the binding screen sets
    // blockNextWhenUnmet, and what it blocks on is a REGISTRY READ - the WinUSB
    // interface guid under the device's Device Parameters. A machine where that read
    // fails for a reason other than "not bound" would strand somebody on a screen
    // telling them to run Zadig when Zadig has already been run, which is the defect
    // 2c998fa removed from page 2 and must not come back through navigation.
    //
    // *** IT IS NOT A DISGUISED Next, AND THE THREE THINGS THAT MAKE THAT TRUE ARE
    //     PROPERTIES OF THE CODE RATHER THAN OF ITS LABEL. ***
    //   - REACHABLE ONLY FROM THE UNMET STATE. overrideLabelOf() in gui.cpp returns
    //     null once satisfied is true, so the control is not on the screen at all on
    //     a machine that is bound - a door that is only a door when there is a wall.
    //   - LABELLED DIFFERENTLY FROM Next. It names what taking it CLAIMS, and
    //     installer/verify asserts the two strings differ.
    //   - IT IS RECORDED. The function below says so through say(), so it is in the
    //     log file, in the console and in a support request, and it sets the flag
    //     that makes this program finish with exit code 3 - done, with something
    //     still pending - instead of 0.
    //
    // *** IT RUNS ON THE WINDOW THREAD, UNLIKE action(). *** action() is a worker
    // because it ends by reading the whole machine again. This reads nothing: it
    // writes one line and sets one bool, so a thread would be machinery with no
    // reason. It returns non-zero when the flow may advance, which is what gui.cpp
    // does with the answer - the door OPENS, it does not silently unblock Next, and
    // Screen::satisfied stays what the machine actually said.
    const wchar_t* overrideLabel; // the named door; null unless blockNextWhenUnmet
    int          (*override)(void* user);

    // ---------------------------------------------------------------------
    // *** THE ONE SCREEN THAT ASKS A QUESTION INSTEAD OF ONLY REPORTING AN ANSWER,
    //     AND THE THREE FIELDS THAT LET IT. ***
    //
    // Every other control in this table ACTS: the offer installs somebody else's
    // program, the door records a claim. This one SELECTS, and a selection is a
    // different shape - it has a current value, it has to survive a re-check
    // rebuilding the whole table, and pressing it must not read the machine.
    //
    // WHY IT IS A PAIR OF LABELS AND NOT A KIND. The screen it is on measures one
    // subject - WHICH mixer this machine has - and reports it in Screen::row like
    // every other check screen. A kind of its own would have bought a fifth arm in
    // every switch over ScreenKind, and MSVC's C4062 makes an unhandled enumerator
    // in a switch with no default an ERROR at /W4 /WX, so it would have been a
    // compile break in every renderer for a screen that renders like the others.
    // The kind says how the screen is DRAWN and it is drawn like a check screen.
    //
    // A null first label is "this screen offers no choice", which is what every
    // other entry in both flows has. Exactly two options are supported and that is
    // deliberate rather than provisional: the driver's profile table has two
    // entries, installer\verify asserts that it still has two, and a third option
    // would have to arrive with a third profile behind it.
    //
    // choiceSelected IS WRITTEN BY buildScreens() AND NOT BY THE WINDOW, which is
    // the whole of how it survives a re-check. rebuildScreens() runs the builder
    // again over a machine that has just been read, so anything the window held
    // itself would be overwritten silently - the shape that cost this project four
    // rounds of content vanishing. The flow owns the choice; the window shows it.
    //
    // choose() RUNS ON THE WINDOW THREAD, like override() and unlike action(): it
    // records a selection and says it. It reads nothing about the machine, so a
    // thread would be machinery with no reason. It returns non-zero when the choice
    // was accepted.
    const wchar_t* choiceLabels[2]; // null first label = no choice on this screen
    int            choiceSelected;  // which one is on, 0 or 1
    int          (*choose)(void* user, int index);

    Row            row;           // this screen's ONE answer, written by buildScreens()
                                  // and restated by refreshScreens()
    bool           satisfied;     // ...and what that answer means for nextAllowed()
};

// ---------------------------------------------------------------------------
// A flow. Plain data, filled in by setup.cpp or uninstall.cpp before the window
// opens. No constructors and no destructors: this project has none of those at
// file scope, and a wizard that ran code at unload would be the first.
// ---------------------------------------------------------------------------
struct Wizard {
    // The flow, in order. Entry 0 is the first screen shown. A zeroed entry ends
    // the table, which is why screenCount() counts rather than being stored: a
    // stored count is a second number somebody has to keep equal to the table.
    Screen screens[kMaxScreens];

    // --- words. All owned by the caller and all in English. ---
    const wchar_t* windowTitle;
    const wchar_t* headline;        // header band, every page
    const wchar_t* subhead;         // header band, second line

    // --- the opening screen's words. WHICH FLOWS HAVE ONE IS NOT A FLAG ANY MORE:
    //     a flow with an opening has an entry of kind kScreenInfo at position 0 and
    //     a flow without one does not, which is why the uninstaller opens straight
    //     on its confirmation without anything having to be told. hasWelcome sat
    //     here after Task 1 as a field written by both flows and read by nobody -
    //     a second answer to a question the table already answers, and the shape a
    //     stale rule takes just before it disagrees with the live one. ---
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

    // *** THE PANE IS NOT A FIELD OF THE FLOW ANY MORE. *** reviewPaneCaption and
    // reviewPaneText sat here and held page 2's walkthrough, and the entry that
    // paints page 2 ALREADY carried the same buffer in Screen::paneText - the same
    // word in two places with nothing forcing them to agree, which is the drift that
    // was closed for the button's verb one round earlier. The pane belongs to a
    // screen, so it is described by the screen: see Screen::paneCaption and
    // Screen::paneText, and layout() reads those and nothing else.
    //
    // What has NOT changed is why the pane exists at all, and it is worth keeping
    // here because it is a property of the control and not of any one screen: it is
    // an EDIT with ES_MULTILINE and WS_VSCROLL and WITHOUT ES_AUTOHSCROLL, so it
    // wraps and its overflow is reachable at any DPI and any window size - a
    // stronger statement than any screenshot. And its text is never written as a
    // literal: the flow SAYS those lines with say(), so they reach the console, the
    // log file, a screen reader and the clipboard, and captures what it said on the
    // way past. The pane and the console cannot drift apart because they are the
    // same bytes.

    // Page 2's Zadig screenshot and the words under it. Null means neither, which is
    // what the uninstaller gets: its page 2 is a removal plan and has no third party
    // window to point at.
    //
    // *** THE PICTURE IS NOT OPTIONAL BECAUSE THE CAPTION IS. *** Setting this asks
    // for both; the picture is dropped in silence if resource IDR_ZADIG_SHOT_PNG
    // cannot be decoded, and the caption is drawn either way. It is written to be
    // read WITHOUT the picture - it names the fields by their labels rather than by
    // where they are - so a failed decode costs a picture and not a sentence.
    //
    // *** IT SAYS WHAT WILL NOT MATCH, AND THAT IS THE HALF THAT MATTERS. *** The
    // screenshot is of a machine that is already bound, so its Driver box and its
    // button read differently from what somebody about to bind will see. The two
    // things they have to MATCH are identical in both states. A picture that promises
    // one screen and delivers another is the defect this installer's text spent two
    // rounds having removed; installer/verify asserts that the caveat is in here and
    // that no single button label is presented as the one to expect.
    const wchar_t* zadigCaption;

    // --- page 3, the work ---
    const wchar_t* progressCaption;
    int            stepCount;
    Row            steps[kMaxRows];

    // --- buttons ---
    //
    // *** THERE IS NO startVerb HERE ANY MORE, AND THAT IS THE POINT. *** It held
    // the one word the foot band's primary button used to be given - "Install" or
    // "Remove" - and after Task 1 the button's words came from
    // screens[i].primaryLabel instead. Keeping both left the SAME word written in
    // two places that nothing forced to agree: change one and the button says one
    // thing while the field says another, which is the caption drift Task 1 closed
    // for kOpeningTitle and its three siblings. The word now exists once, on the
    // screen whose button carries it, and primaryActionFor() below is what says
    // which screen that is.
    //
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
    // Called ONCE, on the worker thread, after the user has pressed the primary
    // button on the one screen whose primaryActionFor() is kPrimaryStart.
    // Returns the process exit code. It may call say*(), askYesNo() and the
    // post*() functions below, and nothing else that concerns the window.
    int   (*work)(void* user);
    void*   user;

    // Page 2's "Check again" button, and the reason this is a FUNCTION POINTER and
    // not a function gui.cpp calls by name.
    //
    // Re-checking means reading the registry and enumerating the process list again.
    // On the window's thread that stops the repainting and earns the window a
    // "(not responding)" in its title, which is the exact impression this window
    // exists to remove. So the re-check is a worker, like work() - and gui.cpp is
    // GIVEN it rather than able to name it, which is what makes "not on the window
    // thread" structural instead of a habit: this file has no declaration of it to
    // call, only a pointer it hands to CreateThread.
    //
    // Called ONCE per press, on a thread of its own, with `user`. It reports rows
    // back through postReviewRow() and returns the number of rows it posted. Null
    // means the page has no button - which is what the uninstaller has, because its
    // page 2 is a removal plan and not a measurement of the machine.
    int   (*recheck)(void* user);

    // ---------------------------------------------------------------------
    // *** THE ONE CONTEXTUAL ACTION THAT USED TO LIVE HERE IS A COLUMN OF THE TABLE
    //     NOW: Screen::actionLabel and Screen::action. ***
    //
    // Why it moved, and it is the whole reason the field could not stay. This was ONE
    // label and ONE worker for the flow, and gui.cpp decided which screen showed them
    // by asking "is this the machine review". So the button could only ever be beside
    // page 2, and page 2 is precisely the page whose accumulation this redesign
    // exists to undo: the offer was about the MIDI port, and a button reading "Install
    // <subject>..." beside a screen about the USB cable is association by position -
    // exactly what the paragraph below refuses. One field cannot describe two screens
    // with two different offers, and the screen after the MIDI port needs TWO buttons
    // at once on one screen.
    //
    // WHAT DID NOT MOVE, because it is still the rule the placement rests on. The
    // button is in the foot band and not on the page: scrollTo() deliberately does
    // NOT pass SW_SCROLLCHILDREN, so a control placed on a scrolling page STAYS STILL
    // while the painted rows slide under it - within one notch of the wheel it would
    // sit beside a row that is not its own, the same failure as posting a row by a
    // stale index and worse for being silent. Passing SW_SCROLLCHILDREN instead drags
    // every child off the top of the window as soon as somebody reads the last row.
    // The foot band is the one strip of this window that never moves.
    //
    // *** THAT CONSTRAINT WAS EXPECTED TO DISSOLVE AND IT DID NOT, AND THE ROUND THAT
    //     EXPECTED IT IS THE ROUND RECORDING SO. *** This block used to end "that
    // constraint dissolves only when the page stops scrolling, which is a later task's
    // work". That task ran, measured the page, and found two screens on which the page
    // cannot stop scrolling: the binding screen needs 399 logical pixels for its title,
    // its rule, its row and the Zadig picture in a page of 398 at 96 DPI, and the
    // install screen needs 433 for its title, its rule and its five rows in the same
    // 398. The arithmetic for both is in allowedDeficit() in installer/verify, and so
    // is the line between the half of it that is arithmetic and the half that is a
    // product judgement - read that before quoting either figure.
    //
    // The binding screen is one of the two, and it is the screen that carries the most
    // controls of any in this program - "Check again" and the named door, side by side.
    // So the page under them still scrolls, ScrollWindowEx() still leaves children where
    // they are, and a control moved onto that page would sit beside a row that is not
    // its own. The placement is unchanged and the reason for it is unchanged; what
    // changed is that it is now a measurement instead of a prediction.
    //
    // AND THE LABEL IS WHAT MAKES A BAND BUTTON SAFE AT ALL. actionLabel NAMES ITS
    // OWN SUBJECT - "Install <subject>...", not "Install" - so the association between
    // the button and the thing it acts on is made of words and not of position, at
    // any scroll offset, any DPI, any window size. The words also have to be able to
    // change: "Install with winget" on a machine that has no winget is a promise the
    // page cannot keep, so the one slot says what the fallback ladder chose.

    // ---------------------------------------------------------------------
    // What a finished re-check means for the screens that carry a row of their OWN.
    //
    // *** IT IS THE ONE CALLBACK HERE THAT RUNS ON THE WINDOW THREAD, AND THAT IS
    //     THE POINT OF IT. *** recheck() and action() are workers because they READ
    // the machine. This reads nothing: it restates the table from the MachineState
    // the worker has already finished writing, and it is called from the handler for
    // "the re-check has finished" - a posted message, so the worker has returned
    // before it runs. Two threads never touch the table.
    //
    // WHY THE ROWS ON PAGE 2 DO NOT USE IT. They come back one at a time through
    // postReviewRow(), addressed by index, WHILE the worker runs - that is what
    // makes the page fill in progressively. A screen's own row has no such channel
    // and needs none, which is the correction this round made: the block above used
    // to say gui.h had "named postScreenRow() as the contract for one", and no such
    // function has ever existed anywhere in this repository - it was a name in a
    // comment, and the two function pointers that were declared beside it have been
    // deleted for the same reason. Building a channel would be new worker to window
    // machinery for news that is a pure function of a MachineState the worker has
    // already finished writing.
    // This is the smaller thing that was available: the news arrives once, at the
    // end, on the thread that paints.
    //
    // Null means the flow's screens carry no measured row - which is what the
    // uninstaller has.
    void  (*refreshScreens)(Wizard* w, void* user);

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
// The three things the flow's shape decides, as PURE FUNCTIONS OF THE TABLE.
//
// installer/verify asserts all three without opening a window, which is the whole
// reason they are functions instead of expressions inside the window procedure -
// an expression inside a message handler is unreachable from any test, and that is
// exactly how the four page model came to have rules nobody could measure.
// ---------------------------------------------------------------------------

// How many entries the flow has. COUNTS, and does not read a stored number: a
// stored count is a second number a human has to keep equal to the table.
int            screenCount(const Wizard* w);

// The words on the foot band's one primary button, for a given screen.
const wchar_t* primaryLabelFor(const Wizard* w, int screen, bool finished);

// ---------------------------------------------------------------------------
// WHAT PRESSING THAT BUTTON DOES, as a value rather than as a branch inside a
// message handler.
//
// *** THIS EXISTS BECAUSE OF THE DESIGN'S RULE 2, WHICH IS "the button names the
//     consequence of pressing it". *** A rule about two things - the WORDS and the
// CONSEQUENCE - can only be checked by something able to read both. The words were
// already readable: Task 1 made them primaryLabelFor(). The consequence was not,
// because it lived in the IDC_PRIMARY arm of frameProc() as a chain of onKind()
// tests, and an expression inside a message handler is unreachable from any test.
// That is exactly how "the opening's button says Install and installs nothing"
// survived six rounds of review: both halves of the defect were visible and no
// single place held them together.
//
// So the handler no longer decides. It ASKS this, and installer/verify asks the
// same function, so a label and its consequence cannot be checked against each
// other in one file and diverge in the other.
// ---------------------------------------------------------------------------
enum PrimaryAction {
    kPrimaryAdvance = 0,  // turns to the next screen. Nothing is written.
    kPrimaryStart   = 1,  // STARTS THE WORK. Exactly one screen per flow, and it
                          // is the only screen allowed to carry the flow's verb.
    kPrimaryClose   = 2,  // closes the window
    kPrimaryNone    = 3   // the button is not offered on this screen at all
};

PrimaryAction  primaryActionFor(const Wizard* w, int screen, bool finished);

// Whether the flow may leave this screen forwards.
//
// *** IT HAS CONSUMERS AS OF THIS ROUND, AND THAT WAS DELIBERATE STAGING. *** It was
// declared, defined and called by nothing for four rounds because nothing set
// blockNextWhenUnmet: wiring it earlier would have been unobservable. The binding
// screen is the first and only entry in either flow that sets it, and the reason is
// consequence and not tidiness - without the MIDI port the audio works and only the
// controls die, and without the WinUSB binding NOTHING works. refreshButtons() greys
// the primary on the answer and IDC_PRIMARY refuses the press on it a second time.
bool           nextAllowed(const Wizard* w, int screen);

// ---------------------------------------------------------------------------
// WHETHER THE FOOT BAND'S PRIMARY BUTTON MAY BE ENABLED ON A GIVEN SCREEN.
//
// *** IT IS A FUNCTION BECAUSE THE EXPRESSION WAS WRITTEN THREE TIMES AND ONE OF
//     THE THREE WAS WRONG. *** refreshButtons() asked both halves; the handler for
// "the re-check has finished" asked both halves; and the recovery path taken when
// CreateThread FAILS asked only the /preview half. On a normal run startBlockedNote
// is null, so that third site handed Next back ENABLED on the one screen in the
// flow that refuses to be left - and the second gate in IDC_PRIMARY then swallowed
// the press. An enabled button that swallows a press is the exact class this
// redesign exists to remove, and it survived because the rule lived in three
// message handlers where nothing could read it.
//
// It answers the STEADY state and deliberately says nothing about g_rechecking:
// that lock belongs to the life of a worker, is applied by the press that starts
// one and is released by the two sites above, so folding it in here would make the
// answer depend on a global and stop it being a pure function of the table.
//
// installer/verify asks it about tables it built itself, and separately reads the
// real button back off the real window after driving the real recovery path.
bool           primaryEnabledFor(const Wizard* w, int screen, bool finished);

// ---------------------------------------------------------------------------
// WHERE THE ZADIG PICTURE WAS LAST LAID OUT, IN PAGE COORDINATES.
//
// *** IT EXISTS SO THAT "THE PICTURE IS ABOVE THE FOLD" IS A MEASUREMENT AND NOT A
//     LOOK AT A CAPTURE. *** The one thing this screen is for is a 574 x 254 picture
// of the most dangerous operation in the installation, and the owner's complaint was
// that it arrived entirely BELOW the fold. renderSubject() records the box it laid
// the picture out in - on the measuring pass as well as on the painting one, because
// the measuring pass is the one installer/verify can run without a window being
// visible - and returns false when the picture was SKIPPED, which is what the page
// does rather than squeeze it when there is not room for it edge to edge.
//
// It is a box and not a height on purpose: the two questions this round has to answer
// are where the picture ENDS against the strip the screen was given, and how WIDE it
// was drawn, and a single number cannot answer the second.
//
// *** IT IS THE RECTANGLE AlphaBlend WRITES, AND IT USED TO BE A RECTANGLE NOTHING
//     DREW. *** drawPicture() paints TWO things: a plate of CLR_PANEL_BG at (x, y),
// S(16) larger than the image in each axis, and then the image itself at
// (x + S(8), y + S(8)). The box recorded here took its ORIGIN from the plate and its
// SIZE from the image, so it was neither - a hybrid whose bottom edge sat S(8) above
// the last row of the picture. Every slack this reported was therefore S(8) too
// generous: 15 px of headroom at 96 DPI where the picture really has 7, and 16 at 144
// where it really has 4. On the one screen whose entire purpose is that the picture
// opens above the fold, the measurement was flattering by more than double.
//
// So it is the image's own rectangle: left and top are where AlphaBlend starts, right
// and bottom are one past where it stops. THE PLATE IS DELIBERATELY NOT IN IT - the
// plate is background, it is S(8) wider than the picture on every side, and its last
// row falls outside the strip at both DPIs. What a reader has to be able to SEE
// without scrolling is the picture, and that is what this reports.
bool           lastZadigShotBox(RECT* out);

// ---------------------------------------------------------------------------
// WHERE THE TWO CHOICE CONTROLS WERE LAST LAID OUT, IN PAGE COORDINATES.
//
// *** IT EXISTS FOR THE REASON lastZadigShotBox() DOES, AND FOR ONE MORE THAT IS
//     SHARPER. *** The radio buttons are REAL WINDOWS, so they are the one thing on
// their screen that no capture can show: renderTall() photographs painted content,
// and a child control is not painted content. "The choice is on the screen and above
// the fold" would otherwise be a claim nothing could measure, on the only screen in
// this program whose subject is a control rather than a sentence.
//
// So renderSubject() records the box it RESERVED for them - on the measuring pass as
// well as the painting one, because the measuring pass is the one installer\verify
// can run without a window being visible - and layout() places the real controls
// inside exactly that box. The harness asserts the box against the strip the screen
// was given, and separately reads the two real windows back off the real page and
// asserts they are inside it. A reserved box the controls did not land in would be
// the picture-above-the-plate defect again, one level up.
//
// False when the screen has no choice, in which case the box is zeroed.
bool           lastModelChoiceBox(RECT* out);

// ---------------------------------------------------------------------------
// WHERE THE DOWNLOAD ADDRESS WAS LAST LAID OUT, IN PAGE COORDINATES.
//
// *** IT EXISTS BECAUSE "THE ADDRESS IS OBVIOUS" HAS TO BE A MEASUREMENT. *** The
// whole point of moving it out of the pane is that a reader meets it without
// scrolling and without hunting, so the two things that have to be true are that its
// box ENDS inside the strip the screen was given, and that the address itself was not
// wrapped or cut - a path landing exactly on a fold, heading readable and address cut,
// is a defect this project shipped once and caught only by looking at a picture.
//
// It is a box and not a height for the reason the picture's is: where it ends and how
// wide it was drawn are two questions, and the second is what says whether the
// address fitted on its line.
//
// Recorded on the MEASURING pass as well as the painting one, because the measuring
// pass is the one installer/verify can run without a window being visible.
//
// False when the screen has no address block, in which case the box is zeroed - which
// is how the harness tells "this screen says nothing about downloading" from "this
// screen says it somewhere I could not find".
bool           lastAddressBox(RECT* out);

// ---------------------------------------------------------------------------
// AND THE PART OF THAT BLOCK A CLICK OPENS, WHICH IS NOT THE SAME RECTANGLE.
//
// *** THE HIT REGION IS THE ADDRESS'S OWN GLYPHS AND NOTHING ELSE, AND THE CHOICE IS
//     THE ONE THING ABOUT THIS FEATURE THAT HAD TO BE DECIDED RATHER THAN DERIVED. ***
// The block is a lead ("Download Zadig here:") plus the address, on one line, and
// lastAddressBox() above spans the WHOLE COLUMN because that is what "does this block
// fit the strip" is about. Three regions were on the table and two are wrong:
//
//   - THE WHOLE BOX. It reaches from the left margin to the right one, so most of it
//     is blank paper. A click on empty page opening somebody's browser is the worst
//     version of this feature, and it would be untraceable to the person it happened
//     to.
//   - THE BOX MINUS THE PADDING, i.e. lead + address. The lead is dim grey small text.
//     Nothing about it says it acts, and a region that acts without looking like it
//     does is the same defect as a button whose words do not name its consequence -
//     Rule 2, one medium over.
//   - THE ADDRESS ITSELF. It is the only thing on the line painted in the accent
//     colour, it is what the owner was pointing at when he called it "o link azul",
//     and it is what a reader aims the pointer at. The mark and the region are then
//     the same object, which is the property the other two lack.
//
// So: the accent-coloured run, at its measured width, and one line tall. In page
// coordinates like the box above, and recorded on the measuring pass as well as the
// painting one, so installer/verify can drive a click without a visible window.
//
// False when there is no address on this screen OR nothing for a click to open, in
// which case the rectangle is zeroed - and that is what makes "no address, no hit
// region" a measurement rather than a promise.
bool           lastAddressLinkBox(RECT* out);

// What the address needed on ONE line, and the width of the column it was given, in
// the font it is really drawn in at the DPI it is really drawn at. Two numbers because
// textBlock() WRAPS instead of clipping: an address wider than its column is not cut,
// it goes onto a second line - and an address read in two halves is an address
// retyped wrong, which is the failure the owner's second decision is about.
void           lastAddressWidths(int* needW, int* haveW);

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
// Worker thread -> window. The only things the worker may say about the window,
// and none of them touches one.
// ---------------------------------------------------------------------------

// The state of step i, and optionally a new detail line for it.
void postStep(int index, RowState state, const wchar_t* detail);

// Replaces one review row, exactly the way postStep() replaces one step row:
// copies what it is given into a heap block, posts it, and returns. `index` must
// be less than Wizard::reviewCount, and it means the same CHECK on every run -
// see the note at the end of fillPreflightRows() for why that is a correctness
// property of this function and not a tidiness one.
void postReviewRow(int index, RowState state, const wchar_t* title,
                   const wchar_t* detail);

// The whole review has been rebuilt. The handler runs on the window thread and is
// where the page relays out and the scroll range is recomputed - once, rather than
// once per row, so a re-check does not make the page jump six times.
//
// gui.cpp posts this itself when recheck() returns, so that a worker which gives up
// half way cannot leave the button disabled for the rest of the run.
void postReviewDone(int newCount);

// *** THERE IS NO postActionOffer() ANY MORE, AND ITS JOB IS DONE BY refreshScreens().
//
// It carried one string from the re-check worker to the window: the words the offer
// had become now that the machine had been read again. That was the right shape while
// the offer was a field of the Wizard, and it became a SECOND way to write the same
// thing the moment the offer became a column of the table - refreshScreens() already
// rebuilds every entry from the fresh MachineState, on the window thread, after the
// worker has finished, and the label is one of the fields it rebuilds. Two channels
// for one fact is how a button comes to say one thing while the table says another,
// which is the class this whole round is closing.
//
// What has not changed is the behaviour that mattered: a re-check that finds the
// outstanding work already done leaves the entry with no label, and a screen with
// no label has no button.
// A page that went on offering work that is done is the same class of statement as a
// summary claiming an install it only attempted.

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
