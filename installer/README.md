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

Four pages:

1. **Opening** - the device, what this installs in two sentences, three lines of
   detail, then a foot band with the credit, the repository address and the non
   affiliation notice, and `Install` / `Cancel`.
2. **Checks** - the four things `common.cpp` detects (ASIO registration, control
   service, teVirtualMIDI, WinUSB binding) plus elevation and which account this
   is for, each with an ok / attention / failure mark. Scrolls when a long path
   makes it taller than the window. Nothing has been changed at this point and the
   page says so.
3. **Progress** - the install steps as a list, each with its state, a progress bar,
   and a pane carrying every line the console would have printed, in a fixed pitch
   font, live.
4. **Finished** - the summary. Not a second version of the console's summary: the
   *same lines*, captured as they are printed, so the "still on your side" list a
   user reads in the window is the console's list character for character. That
   includes the **`NEXT STEPS AND WARNINGS`** block, whose first item is the one
   action that can undo a working install - letting the DJ software fetch
   manufacturer drivers for the mixer. One painted line above the pane points at
   it, so it does not depend on anybody scrolling; the sentences themselves stay
   in the pane, where they are also in the console, in the log file, reachable by
   a screen reader and copyable.

### Modes

```bat
BCD3000Setup.exe              the window
BCD3000Setup.exe /console     the text, exactly as it always was
BCD3000Setup.exe /preview     the window, with the install button disabled
BCD3000Setup.exe /check       the machine report, always in the console
```

`/console` is not a courtesy. It is how a run gets **checked**: identical
decisions, identical text, redirectable, and a shell waits for it. Both programs
are still `/SUBSYSTEM:CONSOLE` binaries for exactly that reason - a windows
subsystem binary returns to the prompt immediately and its redirected output is
whatever happened to be flushed, which would quietly destroy the only way to
verify an install without a person watching. The cost of that choice is a console
window that exists for the few milliseconds before window mode hides and releases
it. That is the trade, and it is the right way round.

`/preview` opens the whole window, including the real machine state on the checks
page, with no path to the steps at all. It is what to run on a machine that must
not be touched: like `/check` it does not even open the log file, because a mode
that claims to write nothing and then writes something is worse than not having
the mode. It exits `4`.

`BCD3000Uninstall.exe` gets the same window minus the opening page: confirm,
progress, summary. Its first page states, **before** the `Remove` button and with
its own mark, that an ASIO driver was registered before this product and that the
uninstaller will offer to register it again. That offer is the difference between
an uninstaller that leaves the machine as it found it and one that leaves it with
no ASIO driver at all, so it is not allowed to be a line somebody scrolls past.
`/yes` keeps the console, because a window that answers its own questions and then
closes would be worse than no window.

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
Startup\BCD3000 Bridge.lnk                                starts it at sign in
%LOCALAPPDATA%\BCD3000Bridge\install.log                  the log of every run
```

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
* **It never installs teVirtualMIDI.** It is a third party product, it is not ours
  to redistribute, and it comes with loopMIDI. The installer detects it and points
  at the download page.
* **It never turns off a Windows protection and never asks you to.** The reason
  this project exists is that the 2010 driver needs driver signature enforcement
  turned off. Suggesting the same thing would defeat the point.
* **It does not stop a running control service** unless asked with
  `/replace-service`, because stopping it destroys the virtual MIDI port and an
  open DJ application will not look for the controller again.

## Building

The two payloads have to exist first. They are build outputs and are not in the
repository.

```bat
rem 1. the driver
cd native\bcdasio
build.bat dll

rem 2. the control service (needs Python and PyInstaller)
cd ..\..\poc
pyinstaller --onefile --noconsole --name BCD3000Bridge bridge_service.py

rem 3. the installer
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

`BCD3000Uninstall.exe`: `0` done, `1` a step failed, `2` bad arguments, `4`
stopped at the user's request. It exits `1` **without removing anything** when the
control service cannot be stopped: that is the case where carrying on would
unregister the driver and then fail to delete its file.

`BCD3000Check.exe`: `0` unless `--self-test` found a problem in the installer's own
helpers. It never fails because of the machine state; that is what the report is
for.
