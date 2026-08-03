# Installer for the BCD3000 open source driver

> **This is an independent, unofficial project. It is not affiliated with,
> endorsed by or connected to Behringer or Music Tribe in any way.** The name of
> the device is used only to say which hardware this driver works with. All
> trademarks belong to their owners. The same notice is on the first page of the
> installer window and in the console banner, from one pair of strings in
> `gui.cpp`, so the three cannot drift apart.

Three small Win32 programs, built with the same MSVC that builds the driver. No
packaging tool is involved and none needs to be installed.

| Program                | Elevation           | Face                | What it does                                                        |
|------------------------|---------------------|---------------------|---------------------------------------------------------------------|
| `BCD3000Check.exe`     | none (`asInvoker`)  | console             | Reads the machine state and prints a report. Changes nothing.        |
| `BCD3000Setup.exe`     | `requireAdministrator` | window, or `/console` | Installs and registers the driver, installs the control service. |
| `BCD3000Uninstall.exe` | `requireAdministrator` | window, or `/console` | Undoes exactly what the setup did, and nothing else.          |

`BCD3000Setup.exe` carries the driver, the control service and the uninstaller
inside itself as resources, so it is the only file a user needs.

`BCD3000Check.exe` stays a console program on purpose. It is a diagnostic tool,
its whole output is what a support request should start with, and text that can be
pasted into a message is worth more there than a window.

## The window

The installer used to run in a console. A black console window that appears by
itself and starts writing to the registry is, to anybody who did not write it,
indistinguishable from something malicious. So there is a window now, and it is
plain Win32: USER32, GDI, COMCTL32 and WIC, all of them Windows components. **No
Inno Setup, no NSIS, no WiX** - the same reasoning that keeps the install logic
hand written applies to its face.

**It used to be four pages, and one of them carried everything.** Page 2 took six
rounds of work - the four detection rows, then elevation and the account, then the
six step walkthrough, then the Zadig screenshot, then its five paragraph caption,
then a contextual action button. Every addition was justified on its own and
reviewed, and nobody ever asked what the page looked like after all six. It needed
1064 logical pixels of a 398 pixel strip: 63 per cent of it was below the fold at 96
DPI and 65 at 144, the picture of the most dangerous operation in the installation
among it. The owner opened `/preview`, looked at that page, and asked for a wizard.

It is **nine screens** now, one subject each.

### The screens are a table, not a switch

`gui.h` carries a `Screen` descriptor and `Wizard` carries an array of them.
`setup.cpp` fills nine entries, `uninstall.cpp` fills three, and `gui.cpp` renders
whatever it is given. Navigation, the word on the primary button and the rule that
refuses to advance are **pure functions of that table** - `screenCount()`,
`primaryLabelFor()` and `nextAllowed()` - which is what lets `installer/verify`
assert all three without opening a window. A subject that arrives later is a new
entry, and an entry carrying two subjects looks wrong in the table itself. That is
the property page 2 did not have.

| # | Screen | Its one subject | Primary button | What else it offers |
|---|--------|-----------------|----------------|---------------------|
| 0 | `An ASIO driver for the BCD3000, without turning anything off` | what this is, who wrote it, and the non affiliation notice | `Start` | - |
| 1 | `Plug the mixer in and switch it on` | is the mixer here, and which of four states this machine is in | `Next` | `Check again` |
| 2 | `Which mixer is this for` | BCD3000 or BCD2000, detected and still choosable by hand | `Next` | two radio buttons, `Check again` |
| 3 | `The MIDI port` | what Windows MIDI Services looks like here, in three states, and the known Windows defect | `Next` | `Check again` |
| 4 | `Get Zadig` | the download only, so that the screen after it has one subject | `Next` | `Open the Zadig page` |
| 5 | `Apply the WinUSB binding` | the dangerous step, alone, with the screenshot | `Next`, **refused while it is missing** | `Check again`, and the named door |
| 6 | `Install the driver` | what will be written and where, and what will not be touched | `Install` | `Check again` |
| 7 | `Installing` | the progress bar and the live log | hidden while it works | - |
| 8 | `The parts this installer owns are in place.` | the summary and the warnings | `Close` | - |

The foot band carries that primary button and one secondary, and **the secondary's
word follows its press rather than its position**: it reads `Cancel` on the opening,
`Back` on screens 1 to 6, and `Cancel` again on `Installing`, where it is greyed
beside the sentence saying why - because there the press does not go back, it stops.
On the last screen it is hidden. `secondaryGoesBack()` is the single expression that
decides both the word and the deed, and `installer/verify` reads the real control
back on every screen, compares it to that expression, and names the three screens
that carry `Cancel`. (This paragraph used to say `Back` on every screen after the
opening, which was never true of the work screen; the window was right and the
sentence was wrong, and the foot band capture of that screen is what showed it.)
It is the only strip of this
window that never moves - and **everything in the last column is in that band too,
except the two radio buttons**, which are children of the page because a radio button
is its screen's subject rather than an action beside it. Rule 2 below says why the
band is the right place and why the withdrawn alternative would have been wrong.

**Six of the nine are steps: 1 to 6.** The opening is not a step and is not counted
as one, and the last two are the work and its summary. `/console` carries the same
six subjects in the same order - five of them in the walkthrough, and which mixer
the run was set up for in the summary, which is where a headless run records it -
and `installer/verify` asserts that screen by screen, against the table's own order,
so the window and the console cannot come to describe two different procedures.

The uninstaller shares this window and inherits the rules, and it is **not** a
wizard: three screens, confirm / work / summary, which already match what it does.

### Rule 1 - no screen has two things that scroll

The defect the owner named was a scroll inside another scroll. The page scrolled,
the review pane scrolled, the pane was anchored to the bottom of the *window* while
the page slid behind it, and neither surface knew about the other - in his own
screenshot the Zadig picture passed underneath the pane.

**One scrolling surface per screen, on 9 of 9 screens at both DPIs**, is what is
asserted now, and it is read off the two real windows: the page's scroll bit and the
pane's visibility, not a claim about the code.

The stronger form - *the page never scrolls* - holds on **7 of 9**. The two
exceptions are declared, with a per screen and per DPI ceiling, rather than bought
by hiding content behind an edge:

| Screen | Overflow at 96 DPI | at 144 DPI |
|---|---|---|
| `Apply the WinUSB binding` | 257 | 448 |
| `Install the driver` | 145 | 292 |

They are not the same kind of number. On the binding screen it is arithmetic: 22 of
start, 23 of title, 28 of rule and gaps, 46 of the screen's own row and 10 of gap
come to 129, and the screenshot is 270 more - 399 against a page of 398, before one
word of the caption or the four bullets. At today's content that screen cannot have
a pane at all; `layout()` would have minus 11 pixels to give one at 96 DPI and minus
23 at 144. On the install screen the five rows **are** the subject - what gets
written, whether we may write it, and who for - and at 144 DPI they alone need about
590 of a 594 page. The window cannot be made taller to escape either: clearing the
binding screen at 96 DPI needs a 817 pixel window, which is 1226 device pixels at
150 per cent scaling, taller than the work area these users have.

So those two keep a scrolling page. That is one scrolling surface each, which is the
rule's heading; it is not the rule's mechanism, and the difference is stated here
rather than paid for by hiding 145 and 257 pixels of a screen's own subject.
`allowedDeficit()` in `installer/verify` holds the two ceilings, and a screen it does
not name is allowed **no** overflow at all.

### Rule 2 - the button names the consequence of pressing it

Page 1 used to carry a button reading `Install` which installed nothing: it turned
the page. This project had already spent two rounds removing *sentences* that
claimed something which had not happened, and went past this one six times because a
button does not read like a sentence.

The label is a promise about what the press does. `Start` on the opening, `Next`
where the press only advances, `Install` on the one screen whose press writes files
and registry keys, `Close` when there is nothing left to do; `Remove` for the
uninstaller, on the one screen that removes. **The word `Install` is on exactly one
screen in the flow**, and `installer/verify` counts it rather than trusting it - and
it reads the label back off the control, so the table cannot describe a word the
window does not write.

**Every pressable control except the two radio buttons is in the foot band, and that
is a correctness decision rather than a layout one.** `scrollTo()` deliberately omits
`SW_SCROLLCHILDREN`, so a control placed on a scrolling page stands still while the
painted rows slide under it - a button sitting beside a row that is not its own. Two
screens still scroll their page, so that constraint has not dissolved; moving the
button onto the six compliant screens and not the other two would put one control in
two places depending on the screen, which is worse than leaving it in one. The band
is the only strip of this window that never moves. `gui.cpp` parents `Check again`,
the contextual action and the named door to the frame for exactly that reason.

Because the button is not beside its subject, **its label names its subject**
(`Open the Zadig page`, never a bare `Open`), so what it
acts on is carried by words instead of by position and means the same thing from any
scroll position. A sentence that names a control its screen does not show is a defect
this project has had thirteen of, and the harness now examines every screen state for
one.

The two radio buttons are the exception and they are children of the *page*, because
a radio button is the subject of its screen rather than an action beside it. That is
safe only because the mixer screen's page does not scroll - `allowedDeficit()` holds
it at zero - and `kChoiceLeadH` in `gui.cpp` records the check that keeps it so.

### Three things these screens carry that are not obvious

**The WinUSB row tells a wrong binding from no binding.** Binding `MI_01` instead of
`MI_00` leaves `MI_00` without an interface guid, which is byte for byte the state of
a machine where Zadig has never been run - so for three rounds the program painted
the same red mark and told somebody who had just run Zadig to run Zadig.
`detectWinUsbBinding()` now enumerates the device's sibling `MI_*` keys and records
**which one** received a guid, and the row says *"interface 1 is bound and it has to
be interface 0"*. Read only, like everything `gatherMachineState()` reaches. The
field is `-1` for "no sibling", and every reader asks for `> 0` and not `>= 0`:
`MachineState` is `ZeroMemory`'d in a dozen places, and `0` read as an interface
number would say "interface 0 is bound", which is precisely what the row has just
denied. `installer/verify` asserts that a zeroed state invents nothing.

**The two text panes are the console's own bytes, not a second copy of them.** The
MIDI port screen's pane is step 1 of the walkthrough in full and the Zadig screen's
is step 2: `setup.cpp` collects the lines those `say()` calls emit, through the sink
every other line already goes through, and hands the buffer to the screen. So a pane
cannot say anything the log file does not, in different words or in a different
order. Painting the text instead would have been easier and is worse in four ways
established in `490904f` - it dies when the window closes, a screen reader cannot
reach it, it does not exist in `/console`, and it is the only kind of text in this
program a small window can cut without anything measuring it.

**The finished screen is not a second version of the console's summary.** It is the
*same lines*, captured as they are printed, so the "still on your side" list a user
reads in the window is the console's list character for character. That includes the
**`NEXT STEPS AND WARNINGS`** block, whose first item is the one action that can undo
a working install - letting the DJ software fetch manufacturer drivers for the mixer.
One painted line above the pane points at it, so it does not depend on anybody
scrolling; the sentences themselves stay in the pane, where they are also in the
console, in the log file, reachable by a screen reader and copyable.

The Zadig screenshot, and the caveat its caption is required to carry, are described
under "The Zadig screenshot" below.

### Modes

```bat
BCD3000Setup.exe              the window
BCD3000Setup.exe /console     the text, exactly as it always was
BCD3000Setup.exe /preview     the window, with the install button disabled
BCD3000Setup.exe /check       the machine report, always in the console

BCD3000Uninstall.exe          the window
BCD3000Uninstall.exe /console the text
BCD3000Uninstall.exe /preview every path and every action a real run would touch,
                              in the order it would touch them, writing nothing.
                              Always text, never a window.
BCD3000Uninstall.exe /yes     no confirmation; keeps the console
```

`/console` is not a courtesy. It is how a run gets **checked**: identical
decisions, identical text, redirectable, and a shell waits for it. Both programs
are still `/SUBSYSTEM:CONSOLE` binaries for exactly that reason - a windows
subsystem binary returns to the prompt immediately and its redirected output is
whatever happened to be flushed, which would quietly destroy the only way to
verify an install without a person watching. The cost of that choice is a console
window that exists for the few milliseconds before window mode hides and releases
it. That is the trade, and it is the right way round.

`BCD3000Setup.exe /preview` opens the whole window and walks all nine screens with
the real machine state on each of them, with **no path to the work at all**: the one
button that would write is refused, on the one screen whose press writes. The screens
are all reachable - screens 1 to 6 are the steps and `/preview` is how you read them
without risk; what the mode removes is the install, not the walk. It is what
to run on a machine that must not be touched: like `/check` it does not even open the
log file, because a mode that claims to write nothing and then writes something is
worse than not having the mode. It exits `4`.

`BCD3000Uninstall.exe` gets the same window minus the opening: confirm,
progress, summary. Its first screen states, **before** the `Remove` button and with
its own mark, that an ASIO driver was registered before this product and that the
uninstaller will offer to register it again. That offer is the difference between
an uninstaller that leaves the machine as it found it and one that leaves it with
no ASIO driver at all, so it is not allowed to be a line somebody scrolls past.
`/yes` keeps the console, because a window that answers its own questions and then
closes would be worse than no window.

### `BCD3000Uninstall.exe /preview` - the dry run of the destructive one

The uninstaller is the program here with the greatest power to destroy a working
installation, and for several rounds it was the one **without** a dry run while the
setup - the less destructive of the two - had one. That asymmetry was the wrong way
round.

`/preview` prints, in the order a real run performs them, all five steps, every path
it would touch, and every action it would take. It writes **nothing of its own**: no
file, no registry key, no shortcut, none of your processes stopped, nothing queued for
the next restart, and **not the log file**. That last one is the write such a mode is most
likely to make by accident - it was made by accident in the first implementation of
the setup's `/preview` and caught before it shipped - so the decision has a name,
`mayOpenLog()`, and it is the **first term** of the condition on the only `logOpen()`
call in the program. `installer/verify` asks that predicate for both answers and then
reads `logIsOpen()` back after running the real `prepare()` in `/preview`, which is
an observation rather than a claim about the order of two statements.

It ends with a list of every way the program is able to write - six with the reason
they were not reached and one that *was* - because "nothing was written" is worth more
as an enumeration than as a sentence, and an enumeration is only worth that if it is
complete.

**There are seven entries on that list and the seventh is a process this mode does
start.** `prepare()` calls `gatherMachineState()`, which calls `detectWinget()`, which
runs `winget.exe --version` with a deadline and calls `TerminateProcess` on it if the
deadline passes. Every mode of this program performs that read - a preview that read a
different machine from a real run would be a preview of something else - so the list
names it rather than rounding down to six, and the entry above it says "none of *your*
processes" instead of the absolute "no process was terminated", which is false on that
one branch. It is the only entry whose effects can land outside this program's own
files, because winget's first run on an account can leave state of its own under
`AppData\Local`. Also recorded, because it is the obvious next step and not this
round's: the uninstaller installs nothing and has no use for winget at all, so the
probe could be skipped under `/preview` outright - the skip belongs in
`gatherMachineState()`, which `BCD3000Setup.exe` shares.

**Three things it says that a plan is normally not honest enough to say.**

* **Whether it would offer to put back the previous ASIO driver, which file that is,
  and whether that file is on disk right now.** This is the step that decides whether
  a machine still has an ASIO driver afterwards, so the preview gives all three facts
  instead of the one.
* **What it cannot know.** Whether the control service can actually be *stopped* is
  not knowable without stopping it, and the preview says so in those words rather
  than implying an answer. So is whether the driver accepts being unregistered,
  whether the previous one accepts being registered again, and whether any file can
  be deleted. Each caveat sits beside the step it qualifies **and** in a collected
  block near the end. It also states the consequence of the one that matters most: a
  real run that cannot stop the service exits `1` **having removed nothing**.
* **Nothing at all in the present tense.** The word "will" appears in no sentence
  `previewPlan()` **writes**, and the harness asserts that absence, because "will" is
  how a description of an intention becomes a promise about a machine nobody measured.
  The claim is deliberately about the function's own prose and not about every line it
  prints, because the preview interpolates paths and a path is not this program's
  writing: an account named `william` would put the word on screen without anybody
  having promised anything. The harness only ever runs the mode against an invented
  machine, so the absence it measures is the real one - and stating the narrower claim
  is the point, since the wider one is false on somebody's laptop.

**It is text and never a window,** which is the one place it deliberately differs
from the setup's `/preview`. The setup's mode exists so that somebody can look at the
*window* on a machine that must not be touched. This one exists so that a list of
paths and actions can be **read** beside the person who owns the machine - which
wants output that is redirectable, pasteable, reachable by a screen reader, and
waited for by a shell. Painted pixels are none of those. The cost, stated rather
than left to be found: there is no way to see the uninstaller's *window* without the
log file being written; the default run gives the window, and only the log separates
the two.

**One function computes the path list, and what that buys is not the same for all
eight paths.** `RemovalPaths` and `computeRemovalPaths()` derive the four paths the
uninstaller used to build inline inside the removal step - the `.bak` and `.new` side
files, the manifest, and the uninstaller's own installed copy. **Those four** are
single sourced by construction: there is one derivation, both the preview and step 4
read it from the struct, and there is nothing for them to disagree about. **The other
four** - `installDir`, `dllTarget`, `bridgeTarget`, `shortcutFile` - were already
single sourced by `gatherMachineState()` before this struct existed, so they are
*copied* into it for the preview to read while `runRemoval()` goes on reading
`state.*` directly. A copy is a second value: it is safe today because the copy is
verbatim, but nothing structural holds it there, and what backs it is a measurement
rather than an argument - `installer/verify` runs `computeRemovalPaths()` on a known
`MachineState` and asserts `wcscmp` equality on all four. This paragraph used to claim
that "the preview cannot name a path the real run would not touch, and the real run
cannot touch one the preview did not name, because neither of them knows how to make
one"; that is an impossibility claim and it is true of four of the eight.

And the `.bak`/`.new` pair is derived a **second time, in the other program**:
`setup.cpp`'s `stagingAndBackupPaths()` builds the same two names, because the setup is
what writes those files. Single sourcing inside `uninstall.cpp` cannot see that one, and
a disagreement there is silent and permanent - a setup that wrote `.bak` against an
uninstaller that looked for `.bkp` would leave a copy of a driver in the install folder
after every uninstall, which is also what stops `RemoveDirectoryW`, so the folder would
survive too. The harness calls **both** functions and compares their answers, with no
literal in the middle. That is the only path check here that can catch a *wrong*
derivation rather than a missing one: the eight checks that compare `run.paths.X`
against text printed from `run.paths.X` share an origin on both sides, so a
`computeRemovalPaths()` that built `.bkp` would move both sides together and stay green.

`RemovalPaths` carries **paths and not existence**: the preview reads what is on disk as it prints
and says "at this moment", and the real run reads it again when it gets there, after
a process has been stopped and a file possibly released. What was deliberately *not*
done is table-driving the deletions themselves: a file, a locked file, this process's
own executable and a directory need `DeleteFileW`, `MoveFileExW` with
`MOVEFILE_DELAY_UNTIL_REBOOT` and `RemoveDirectoryW`, with different messages and a
**load bearing order** between two of the pending operations, and collapsing four
different operations into one loop would be a rewrite of the part of this program
that has been validated on hardware. The residual cost is written into the code:
somebody adding a fifth path has to add it in both places, and nothing fails if they
add only the branch.

### Not blocking the window

Every step runs on a worker thread. The window's thread never does file or
registry work, because a window whose thread is busy stops repainting and Windows
greys it out and adds "(not responding)" to the title - which looks exactly like
the thing this window exists to stop looking like.

The worker never touches a window, a control or a device context. Its only three
ways of reaching the window are: `say*()`, which `common.cpp` hands to a line sink
that copies the text and `PostMessage`s it; `bcdgui::postStep()`, which posts;
and `askYesNo()`, which `SendMessage`s and blocks until the window's thread has
put a dialog up and got an answer. Every read and write of a control happens
inside the window procedure on the main thread.

Cancelling is honest. Before the first write, `Cancel` closes the window and
nothing has happened. After it, the button is greyed next to the sentence *"This
cannot be stopped once it has started."*, and closing the window says why in
words instead of half undoing an install. There is no point in the middle of
writing a driver file and a registry key where stopping leaves the machine in a
state anybody chose.

### DPI

`admin.manifest` asks for **PerMonitorV2**, with per monitor v1 and then the older
`dpiAware true` as fallbacks. PerMonitorV2 is the only mode where Windows also
scales the caption and the dialogs and tells the program about a DPI change;
`gui.cpp` handles `WM_DPICHANGED` by rebuilding the fonts, decoding the device
photograph again at the new pixel size, and laying the page out again. Without any
declaration Windows scales the window as a bitmap, and on a high density screen
the result is soft, smeared text - which is the "something cheap is running" look
all over again.

**The header band's height comes from the fonts, not from a constant.** It used to be
four numbers - the band 70 logical pixels tall, the headline in a box 26 tall, the
subhead in one 20 tall - and `DrawTextW` **clips to the box it is given**, so the tail
of the `g` in "Behringer" was cut off. Not at one DPI: the font's own cell is 30
pixels at 96 and 45 at 144, against boxes of 26 and 39, so it was cut at both, by
three rows and by five, and a correction in fixed pixels would have had to be wrong
at one of them. `headBand()` asks the two fonts for `tmHeight` - ascent plus descent,
the whole cell, descenders included - and lays the band out from that. `kHeadH` is
now a **floor**: at 96 DPI the arithmetic lands on exactly 70, so the band nobody
complained about does not move; at 144 it asks for 108 where 1.5 x 70 is 105. One
function answers both the painter and `layout()`, because the band's height is where
the page begins.

That is measured rather than asserted. `installer/verify` renders the band into a
bitmap and compares its ink with the ink the same fonts produce **with nothing to
clip them** - not with a number, because a box that clips its text produces ink that
stops flush against it, and flush is sometimes correct: the subhead's descenders
legitimately land on the very last row of their cell at 96 DPI. The comparison
exists because the pixel proof used to measure only the *page*, and the band is
painted outside it, so eight of eight clipping checks passed over a defect an eye
caught in a second. A number that looks like it covers everything is the most
dangerous kind of hole.

### The device photograph

`docs/BCD3000.PNG`, embedded as resource **110** in `setup.rc` and decoded at run
time with WIC. **One file, one id, one reference** - replacing the picture is
replacing that file and rebuilding. It is not copied into this folder.

> **The photograph is a manufacturer product photograph. It is NOT covered by this
> project's licence,** and it is **not in this repository** - the same treatment
> `.gitignore` already gives the ASIO SDK and the 2010 Behringer driver: third
> party material that this project may not redistribute stays on the disk to build
> with and out of the tree. `build.bat setup` checks for it first and says so.
>
> To build with a different picture, put a 32 bit PNG with an alpha channel at
> `docs/BCD3000.PNG`, or point resource 110 somewhere else. The window degrades
> without complaint if the resource cannot be decoded: the text simply moves left.

WIC rather than converting to a BMP during the build, for three reasons that point
the same way: a build time conversion needs a third party tool, which this project
has just finished removing from its build; it creates a generated file that either
goes into the repository or has to be rebuilt by everyone who clones; and a 32 bit
BMP's alpha channel is not reliably honoured, so the practical BMP route is to
flatten the transparency onto a chosen background colour, after which the picture
is only correct on that exact colour. WIC also scales at decode time with a proper
filter, which is what keeps it sharp at 150%.

The **ASIO logo is deliberately not used anywhere.** It carries its own trademark
rules and it is out of this repository.

### The Zadig screenshot

`docs/Zadig.png`, 574 x 254, 8 bit RGBA, embedded as resource **111** in `setup.rc`
and decoded by the same WIC path as the photograph. **One file, one id, one
reference.** It is on the screen called **Apply the WinUSB binding**, at its
**native 574 logical pixels** - one pixel per pixel at 96 DPI - with its caption
underneath.

**It used to be on the checks page and that is what the owner rejected.** It sat
under seven rows on a page that needed 1064 logical pixels of a 398 pixel strip, so
it arrived **entirely below the fold**, with its caption, the account row and the
footer: 63 per cent of that page needed scrolling at 96 DPI and 65 at 144. It has a
screen of its own now and it is drawn **between the row and the bullets**, so the
whole of it is on the screen when the screen opens - `installer/verify` measures the
box the renderer laid it out in against the strip the layout gave that screen, at
both DPIs, and the screen has **no pane** for that reason: a screen with a pane gets
221 logical pixels at 96 DPI and the picture alone is 254.

> **It is in this repository, unlike the photograph, and it is a different case.**
> The photograph is third party material being redistributed. This is a screenshot
> of the owner's own machine, taken by the owner, showing the owner's own device.
> Zadig is GPLv3, and that licence covers the program, not a picture of its window.
> It was checked for a user name or a path: clean.

**Why native size and not beside its caption.** The page is 683 pixels wide at 96
DPI; the picture is 574 of them. Putting the caption beside it would need the
picture at roughly half size, and the two strings this picture exists to be matched
against - `BCD3000 (Interface 0)` and `USB ID 1397 00BF` - are 9 pixel text in the
original. At half size they are not text any more. A picture of the most dangerous
step in the installation that cannot be read is worse than no picture, because it
invites somebody to believe they have matched something they cannot see. So the
caption is full width underneath, and `installer/verify` measures at both 96 and 144
DPI that the page is still wide enough for the picture to be **drawn** rather than
skipped, and that nothing on the page is clipped.

**The caption's caveat, which is not optional.** The screenshot is of a machine that
is **already bound**, so two of its fields show a state a user about to run Zadig
does not have:

| Field | In the screenshot | On a machine that is not bound yet |
|---|---|---|
| `Driver` (left box) | `WinUSB (v6.1.7600.16385)` | `usbaudio`, or whatever Windows put there |
| the button | `Reinstall Driver` | most likely `Replace Driver` or `Install Driver` |

This does not invalidate the image, because **the two things the user has to match
are identical in both states**: the line `BCD3000 (Interface 0)` in the list, and
`USB ID 1397 00BF`. The caption points at those two and declares the other two.
**Declared limit:** Zadig chooses the button's word from what is already bound
(`Install` / `Replace` / `Reinstall` / `Upgrade`) and not every variant has been
seen here, so the caption says *"it is the large button, whatever it is called"* -
true without inventing precision. `installer/verify` asserts that the caption names
**more than one** possible label, so that no single one can be presented as the one
to expect. An image that promises one screen and delivers another is the same defect
two rounds of this project removed from this installer's text.

**It degrades without complaining.** A resource that cannot be decoded costs the
picture and nothing else: the caption is drawn either way and the text simply moves
up. The caption is written to be read without the picture - every field is named by
its own label, never by where it sits in the image - and the harness asserts that
too. Every fact in the caption is **also** said by
`printPrerequisiteWalkthrough()` through `say()`, so it reaches the pane, the
console, the log file, a screen reader and the clipboard; painted text is the one
kind of text in this program that does none of those.

### Microsoft's `Windows.Devices.Midi2.dll`, and the notice that has to travel with it

The virtual MIDI port is created through **Windows MIDI Services**. Our own
`BcdMidi.dll` calls it, and that call needs Microsoft's runtime,
`Windows.Devices.Midi2.dll`, in the same directory as the process. So the installer
carries **two** DLLs for the control service and writes both beside it:

| Resource | File | Where it comes from |
|---|---|---|
| **104** | `BcdMidi.dll` | ours, `native/bcdmidi/build.bat dll` |
| **105** | `Windows.Devices.Midi2.dll` | Microsoft's, `native/bcdmidi/wms/` |

> **Resource 105 is somebody else's binary, redistributed inside ours, and it is the
> only one in this product.** It is **Copyright (c) Microsoft Corporation**, from
> [github.com/microsoft/MIDI](https://github.com/microsoft/MIDI), under the **MIT**
> licence. MIT requires the copyright notice and the permission notice to be included
> with every copy of the software, so they are reproduced **in full** in the
> repository's [`LICENSE`](../LICENSE), under *Third party software redistributed by
> this project*. That is a licence condition, not documentation: shipping the DLL
> without the notice is shipping it without permission.
>
> This is the whole point of the change that brought it here. The library it replaced
> came with an SDK header saying *unauthorized usage and distribution is prohibited*,
> which is what stopped this product being published at all. Microsoft's project
> answers the same question the other way, in its own FAQ: *"Q: Can I sell an
> application which uses Windows MIDI Services / A: Yes. Of course."*

**It is x64 and there is an arm64 build of the same name.** The x64 one is
**2,892,288 bytes**; the arm64 one is 6,111,744. `installer/verify`'s `exe` mode reads
the size of resource 105 out of the built installer and compares it with 2,892,288
written as a literal, so the wrong architecture cannot be shipped quietly. Take it
from `runtimes/win-x64/native/` inside the NuGet package.

**No manifest and no registration.** C++/WinRT falls back to `DllGetActivationFactory`
on a DLL sitting beside the executable, and PyInstaller's one-file bootloader calls
`SetDllDirectoryW(_MEIPASS)`, so "in the process directory" holds for the packaged
control service too. Measured on 2026-08-01. Nothing is registered and no manifest is
generated - which is why "write it next to the bridge" is the whole of the deployment.

**Neither DLL is in this repository.** `.gitignore` excludes `*.dll` everywhere, ours
included, the same treatment `native/ASIOSDK/` gets. `build.bat setup` checks for both
before calling `rc` and says which one to build.

### The icon

`bcd3000.ico`, on all three executables, at 16/20/24/32/40/48/64/128/256 as
uncompressed 32 bit entries. Original artwork of this project: three channel
faders at three different heights on a rounded badge. Geometric on purpose - it
has to survive being 16 pixels wide, and the asymmetry is not decoration. The
first attempt was two platters above a crossfader, which is a fine picture of a
mixer and reads as a cartoon face at every size.

It is committed as a source asset, like the photograph, and it is the only binary
this folder adds to the repository.

## Why a hand written executable instead of a packaging tool

Nothing else is on the machine: no Inno Setup, no NSIS, no WiX, no PowerShell
installer module. The alternatives were `IExpress`, which ships with Windows but
can only unpack files and run one command, and this. A Win32 program adds no
dependency at all to the project, and every requirement below needs real logic
rather than a file list:

* detect the WinUSB binding and **refuse to touch it**;
* detect a registration that points somewhere else, ask before repointing it, and
  **record it so the uninstaller can put it back**;
* refuse to replace the control service while it is running, because stopping it
  destroys the virtual MIDI port;
* compare file contents so that a second run changes nothing;
* check the driver payload **before** it is allowed to replace a driver that
  works;
* verify each step by reading the result back.

## What gets installed where

```
%ProgramFiles%\BCD3000 ASIO Driver\BcdAsio.dll            the ASIO driver
%ProgramFiles%\BCD3000 ASIO Driver\BcdAsio.dll.bak        the driver it replaced
%ProgramFiles%\BCD3000 ASIO Driver\BCD3000Uninstall.exe   the uninstaller
%ProgramFiles%\BCD3000 ASIO Driver\install-manifest.txt   what was done, in text
%LOCALAPPDATA%\BCD3000Bridge\BCD3000Bridge.exe            controls and LEDs
%LOCALAPPDATA%\BCD3000Bridge\BcdMidi.dll                  the virtual MIDI port
%LOCALAPPDATA%\BCD3000Bridge\Windows.Devices.Midi2.dll    Microsoft's runtime
Startup\BCD3000 Bridge.lnk                                starts it at sign in
%LOCALAPPDATA%\BCD3000Bridge\install.log                  the log of every run
```

The two DLLs go **beside the control service** and nowhere else: that directory is
the process directory of the thing that loads them, which is all registration-free
WinRT activation asks for. `Windows.Devices.Midi2.dll` is Microsoft's and is
redistributed under the MIT licence - see *Microsoft's `Windows.Devices.Midi2.dll`*
above, and [`LICENSE`](../LICENSE) for the notice itself.

`BcdAsio.dll.bak` only appears when an install replaced an earlier driver file.
The payload is written next to the target as `BcdAsio.dll.new` first and only
takes the target's place after it has been checked - an x64 PE DLL that Windows
loads and that exports `DllRegisterServer`. Writing first and loading afterwards
is how a corrupt payload used to destroy a working driver and only then fail. The
uninstaller removes both side files.

Registry, written by the driver's own `DllRegisterServer`:

```
HKCR\CLSID\{B0D3000A-51E7-4C2B-9F3A-1234ABCD5678}\InprocServer32   -> the DLL path
HKLM\SOFTWARE\ASIO\Behringer BCD3000                               -> CLSID, Description
```

The driver goes into `%ProgramFiles%` and not into the user profile on purpose.
It is loaded into the DJ application, which is often elevated, so a DLL in a
location the user can write to would be a way for any program running as that
user to get code into an elevated process. `%ProgramFiles%` is writable by
administrators only.

The control service goes into the user profile and starts from a **Startup
shortcut**, which is what keeps it **unelevated**. That is load bearing, not a
detail: the service is the server of the local channel that carries the controls
and the LEDs, and the driver inside the DJ application is the client. An elevated
process can open an object created by a normal one; the other direction is what
usually gets refused. Replacing the shortcut with a scheduled task that runs with
highest privileges would silently kill the controls on machines where the DJ
application is elevated.

### Which user's profile

The per user folders are resolved from the token of the account that owns the
desktop, taken from `explorer.exe`, and **not** from the account the installer
runs as. The two differ whenever Windows asked for an administrator's *password*
instead of a plain "Yes": the process then belongs to that administrator while the
desktop belongs to somebody else.

When they differ the setup **refuses the per user half** and says so. Writing into
its own profile would install the service for an account that never signs in;
writing into the other account's profile would mean an elevated process writing
into a tree that account can modify, which is a way to aim an elevated write
somewhere else by replacing a folder with a junction. The driver half is machine
wide and still gets installed, so re-running from the right account finishes the
job. In that situation no log file is written either, for the same reason.

## Undoing an install without making the machine worse

The ASIO registration is machine wide and holds **one** driver at a time.
Installing therefore points every host application away from whatever was
registered before, and that path is the one value the uninstaller cannot work out
for itself. It is written to `install-manifest.txt` as
`previous_asio_registration`, and:

* a **second** run of the setup has nothing of its own to record, because the
  registration already points at the install folder. It reads the recorded value
  back and keeps it. An absent value never overwrites a recorded one;
* the **uninstaller** checks that the recorded file is still on disk and offers to
  register it again, then proves it by reading the registry back. If that is
  declined, the `regsvr32` line is the **first** thing in its summary;
* if the setup fails **during** registration, its summary prints the same line.
  The ASIO SDK's `RegisterAsioDriver` deletes the old class id entry before
  writing the new one, so a failure in between leaves nothing registered at all.

## What the installer refuses to do

* **It never rebinds the USB device.** The binding is applied once, by hand, with
  Zadig. A wrong binding leaves the hardware unusable, and the installer has no
  way to verify a rebind before the user finds out the hard way. It detects the
  binding and prints instructions when it is missing.
* **It never creates a MIDI port, not even to test one.** Screen 3 reads whether
  Windows MIDI Services is there and whether this build carries a known defect,
  and says so. It does not create a port, so it never tells you one will work -
  and under `microsoft/MIDI` issue #1047 the first virtual port after a restart
  is the only one that can be created, so an installer that made one to test it
  would spend the machine's only port on the test.
* **It never turns off a Windows protection and never asks you to.** The reason
  this project exists is that the 2010 driver needs driver signature enforcement
  turned off. Suggesting the same thing would defeat the point.
* **It does not stop a running control service** unless asked with
  `/replace-service`, because stopping it destroys the virtual MIDI port and an
  open DJ application will not look for the controller again.

## The MIDI port: Windows MIDI Services, and the Windows defect screen 3 exists for

A previous round of this installer detected a specific third party virtual MIDI
product here and offered to start its author's own installer through winget.
That whole subsystem is gone: the product could not be redistributed under this
repository's licence, and this repository is going public.

What replaced it is **not another install instruction**. The port is created
through **Windows MIDI Services**, which is part of Windows, so there is nothing
for this installer to offer. Screen 3 is a reading, and it has three states:

| state | what the screen says |
|---|---|
| service present, build not on the known-bad list | nothing to install, and it names what it looked at: the `midisrv` service, `Midi2.VirtualMidiTransport.dll` and its version, and the build. It states that no port was created, so nothing there says one will work. |
| service present, build on the known-bad list | names `microsoft/MIDI` issue #1047, says the fix arrives through Windows Update rather than from anybody here, and says restarting the machine clears it if it has already bitten. |
| anything else | prints the numeric result of each read - service registered 0/1, transport present 0/1, versions read 0/1, and the error code - and states no cause. |

**The reads are three, all cheap, and none of them is a port:** the SCM is asked
whether it knows `midisrv`; `Midi2.VirtualMidiTransport.dll`'s version resource
is read out of the system directory; and `midisrv.exe`'s version resource gives
the `build.revision`. Nothing is loaded as a module and no port is ever created.

**The known-bad list is in one place**, `kKnownBadMidiBuilds` in `common.cpp`. It
is a range per servicing branch - build, first bad revision, first fixed revision
- and it is the only thing that changes when Microsoft ships the fix. It is a
range rather than the two literal builds `microsoft/MIDI` issue #1047 names
because the machine this was measured on is at `26100.8972`, past the `.8875`
that KB5101650 introduced, and the defect still reproduces there.

**The version of `Midi2.VirtualMidiTransport.dll` is not a Windows build number.**
It reads `1.0.15.0` - the Windows MIDI Services component version - which is why
the build test is taken off `midisrv.exe` and not off the transport DLL.

## Building

The payloads have to exist first. They are build outputs, or somebody else's
package, and none of them is in the repository.

```bat
rem 1. the driver
cd native\bcdasio
build.bat dll

rem 2. the MIDI port, and Microsoft's runtime DLL copied beside it
cd ..\bcdmidi
build.bat dll

rem 3. the control service (needs Python and PyInstaller).
rem     FROM THE .spec, NOT from a bare command line: a generated .spec does
rem     not carry the two MIDI DLLs, and `pyinstaller ... bridge_service.py`
rem     would overwrite the one that does. The build would still succeed.
cd ..\..\poc
pyinstaller --noconfirm BCD3000Bridge.spec

rem 4. the installer
cd ..\installer
build.bat all
```

`build.bat all` builds `check`, then `setup`. The `setup` target **builds the
uninstaller itself**, because it embeds it: leaving that to the caller means a
stale `BCD3000Uninstall.exe` on disk gets shipped inside a freshly built
installer, and every check in the script would be satisfied.

Each target prints `BUILD_OK` only when the compiler's exit code was zero **and**
the output file exists. Both conditions, never only one: a verdict taken from the
file's existence alone reports success when the link failed and a stale binary is
still on disk. That defect was real in this project once and is not coming back.

```bat
build.bat strict
```

`strict` compiles **seven units** at `/W4 /WX`, **compile only** (`/c`, objects in
`strict\`), so it produces no binary and replaces nothing that may be in use: the five
sources in this folder, `verify/bcdverify.cpp`, and `../native/bcdmidi/bcdmidi.cpp`.
It needs none of the payloads the `setup` target requires. It does need the C++/WinRT
projection for that seventh unit, so it is **no longer the one target a freshly cloned
machine can run** - run `native\bcdmidi\build.bat dll` first, which generates it. The
target checks for the projection and names that command rather than skipping the unit:
a target that compiles six and still prints seven is the class of verdict this script
exists to stop. `/WX` is on this
target and on no other, deliberately: a new compiler warning must not stop somebody
producing a binary to test on the hardware, but it must not go unnoticed either. This
is `native/bcdasio/build.bat`'s pattern, and `installer/` was the last corner of the
project without it. The rationale is the same one written there, and it earned itself
again in this folder: the round that added `/preview` produced a `C4429` and four
`C4129` warnings about a mangled escape sequence in the harness, the warnings scrolled
past, and the defect shipped. Under `/WX` that is `BUILD_FAIL` and exit code 1. Proved
by negative before it was believed - a warning was injected, the target failed, the
injection was reverted and the file's md5 confirmed.

Nothing built here is committed: `*.exe`, `*.obj` and `*.res` are ignored by the
repository's `.gitignore`. The two things in this folder that are **not** build
outputs are `bcd3000.ico`, which is source artwork, and the reference in
`setup.rc` to `../docs/BCD3000.PNG`, which is already tracked where it lives.

## Testing without installing

```bat
BCD3000Check.exe --self-test
```

`--self-test` exercises the path building, the file comparison used for
idempotence, and the constants that are contracts with code outside this folder
(the class id, the ASIO registry name, the device enumeration key, the service and
shortcut names). A mismatch in any of those breaks nothing at build time and makes
the installer act on the wrong thing at run time, which is why they are asserted
as text.

The only file `--self-test` writes is one uniquely named file in `%TEMP%`, which
it deletes again. Without the switch the program writes nothing at all.

## Exit codes

`BCD3000Setup.exe`: `0` done, `1` a step failed, `2` bad arguments, `3` done but
something is still pending on the user's side (a missing prerequisite, or a
component deliberately not replaced), `4` stopped at the user's request.

The same codes in both modes. Closing the window before pressing the install
button is the same answer as declining at the console prompt, and `/preview` exits
`4` because nothing was changed.

`BCD3000Uninstall.exe`: `0` done, `1` a step failed, `2` bad arguments, `4` **nothing
was changed** - either stopped at the user's request or `/preview`. It exits `1`
**without removing anything** when the control service cannot be stopped: that is the
case where carrying on would unregister the driver and then fail to delete its file.

`4` covers `/preview` because it is the only code in this program's table whose
meaning is "nothing was changed", which is the single most important fact about a
preview run - and because the alternatives are worse rather than merely different.
`0` means "the driver and the control service have been removed", so a caller checking
for `0` after a preview would be told the machine had been uninstalled. `1` means a
step failed, and no step ran. `2` means the command line was wrong, and it was not.
The symmetry with `BCD3000Setup.exe /preview` is a consequence of that argument and
not the reason for it.

**A preview exits `4` even on a machine where a real run would be refused.** The code
is a statement about what *that* run changed, which is nothing, in every case. It is
never a prediction of the real run's exit code: returning a predicted code would be
exactly the "this would work" claim the mode refuses to make. What the refusal *is* is
printed - which code a real run would exit with, that it would remove nothing, and
that the previous-registration question cannot be answered because a real run refuses
before it reads the manifest.

`BCD3000Check.exe`: `0` unless `--self-test` found a problem in the installer's own
helpers. It never fails because of the machine state; that is what the report is
for.
