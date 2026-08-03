# An open source driver for the Behringer BCD3000 on Windows 11

> **This is an independent, unofficial project. It is not affiliated with, endorsed by or
> connected to Behringer or Music Tribe in any way.** The name of the device is used only to
> say which hardware this driver works with. All trademarks belong to their owners.

A user-mode **ASIO** driver plus a small control service that give a Behringer BCD3000 back
its **4 audio channels** and its **complete control surface** — knobs, buttons and LEDs — on
Windows 11, where the manufacturer's driver no longer loads.

Everything runs in **user mode**. No kernel component is built or installed, and **no Windows
protection has to be turned off**: Secure Boot and HVCI stay on. That is the whole point of
the project, and it is the reason it is built the way it is.

---

## ⬇️ Download

### **[Download the installer — v1.0.0 (ZIP, 9.2 MB)](https://github.com/rptmatrox/Behringer-BCD-driver-win11-64x/releases/latest/download/BCD3000-driver-v1.0.0-win11-x64.zip)**

Unpack it and run **`BCD3000Setup.exe`**. The ZIP also carries the uninstaller, a read-only
machine check, and the same instructions in two languages — `README.txt` in English and
`LEIA-ME.txt` in Portuguese.
[All releases and full notes](https://github.com/rptmatrox/Behringer-BCD-driver-win11-64x/releases/latest).

**Read these four lines before you install — they are the ones that surprise people:**

- **Windows 11, 64-bit, is required.** Microsoft's documentation states that Windows MIDI
  Services requires Windows 11. See [Windows 10](#windows-10) for what that means for the
  audio half.
- **Windows will warn you that it does not recognise the program.** The binaries are not code
  signed, because a certificate costs money this project does not have. Click *More info* →
  *Run anyway* — or read and build the source yourself, which is all here.
- **One manual step remains: Zadig**, which binds the device to WinUSB. The installer explains
  it when the moment comes.
- **Some Windows 11 builds carry a defect Microsoft has acknowledged**
  ([microsoft/MIDI #1047](https://github.com/microsoft/MIDI/issues/1047)): on those, only the
  **first** virtual MIDI port created after a restart works. **Restarting the machine always
  clears it.** The installer detects this and tells you; there is nothing for you to install,
  and the fix arrives through Windows Update.

Prefer to build it yourself? See [Building it](#building-it).

---

## Why this exists

The BCD3000 is a DJ controller from 2005 and its official Windows driver is from **2010**.
That driver is a kernel-mode driver signed with a **SHA-1 certificate, cross-signed for
Windows 7**. Windows 11 24H2 and later no longer trust that chain, so the driver fails to
load with **code 39** and the hardware becomes a paperweight — no audio, no controls.

The hardware itself is fine. It is a USB 1.1 full-speed composite device that speaks
perfectly ordinary isochronous audio and USB-MIDI. So instead of trying to make a 2010
kernel driver load, this project talks to it from **user mode over WinUSB**, which Microsoft
ships and signs itself.

---

## Status

| | |
|---|---|
| Audio out | **4 channels** — Master L/R and Phones L/R |
| Audio in | **4 channels** — Phono A L/R and Phono/Line B L/R |
| Sample rate | **44.1 kHz only.** 48 kHz is refused with a clear message, because the hardware does not do it |
| Control surface | **All of it** — knobs, buttons, jog wheels, and the LEDs light up |
| Tested with | VirtualDJ 2026 (b9482) on Windows 11 Pro 26200, x64 |

**Measured, not estimated:** an uninterrupted run of **82 min 56 s** at an average of
**44,095.7 samples/s** with **zero underruns, zero overruns** and zero input starvation, with
audio and the full control surface active at the same time. Pulling the USB cable out
mid-track and plugging it back in recovers **on its own** — the audio restarts and the
controls come back **without reopening the DJ software**.

**Known limits, stated plainly:**

* The device's clock and the PC's clock drift apart by about **0.03%**. This is corrected in
  a closed loop (measured: 77.9 B/s of compensation against 79 B/s of drift), and the input
  buffer level was flat across all 83 minutes. Without the correction it overflows in about
  12 minutes.
* Reported input latency is an **estimate**, not a loopback measurement: about **73.9 ms** at
  a 512-frame buffer, 41.9 ms at 256, 265.4 ms at 2048. Output latency is the ordinary
  three-transfer figure. A real loopback measurement is future work.
* **Use a USB 2.0 port.** The device is USB 1.1 full speed and its audio is isochronous,
  which some USB 3.x controllers do not carry reliably. That is hardware; there is no
  software fix.
* Only one program can hold the device at a time. The driver and the control service hand it
  back and forth automatically, so this is invisible in normal use — but a virtual machine's
  USB arbitrator can steal it. (VMware took it twice in one day on the development machine.)
* **A known Windows defect can make the virtual MIDI port fail to recreate until the next
  reboot.** Tracked upstream as `microsoft/MIDI` issue #1047, acknowledged by Microsoft with
  no fix date. Details, and what the installer checks for it, are under Step 3 in
  [Installing it](#installing-it) below.

### Hardware support

| Device | Audio | Control surface |
|---|---|---|
| **BCD3000** (`1397:00BF`) | validated on hardware | validated on hardware |
| **BCD2000** (`1397:00BD`) | *should* work — untested | **not supported** |

The BCD2000's audio interface is **identical** on paper: same endpoints, same 16-bit
4-channel 44.1 kHz format, same 8 bytes per frame. Its control surface is **a different
device** — a proprietary MIDI framing instead of standard USB-MIDI, plus a mandatory 52-byte
initialisation sequence. The driver validates the packet arithmetic before it starts and
**refuses with a clear message** if it does not add up, so the outcomes are "it works" or "it
explains why not" — never corrupted audio.

**Nobody on this project owns a BCD2000.** If you do, please
[open an issue](https://github.com/rptmatrox/Behringer-BCD-driver-win11-64x/issues) — the
audio path may well already work for you, and the controls can be added with a USB capture
from real hardware.

---

## Installing it

**Right now this is five steps, and one of them is somebody else's program.** That is the
honest answer, and shortening it further is the project's main open problem — see
[The goal is one installer](#the-goal-is-one-installer) below.

> **Before you start: Windows SmartScreen will warn you about the download.** The installer
> is not signed with a code-signing certificate, because those cost money every year and this
> project is free. SmartScreen says "Windows protected your PC" for *every* unsigned
> executable, whatever it does. If that is not acceptable to you, build it yourself from
> source — the whole thing is here and it needs nothing but Visual Studio Build Tools.

### Step 0 — Plug the BCD3000 into a **USB 2.0** port and switch it on

**Do this first, before anything else.** Windows has to enumerate the device once before
Zadig has anything to replace. It also lets the installer tell "this machine has never seen
the device" apart from "the device is here but not bound yet", which are different problems
with different fixes.

### Step 1 — Run **Zadig** once, to bind the device to WinUSB

* Download page: **https://zadig.akeo.ie/**

This is how the driver reaches the hardware. Nothing works before it. **It is also the most
dangerous step in the whole process**, so do it carefully and read all four points:

1. **`Options` → `List All Devices` first.** Without this the BCD3000 does not appear in the
   list at all, because it is a composite device. This is the mistake everybody makes.
2. In the dropdown, pick **`BCD3000 (Interface 0)`**. Check that the **USB ID** below reads
   **`1397`** and **`00BF`**. Interface 0 is the function Windows calls `MI_00`. **It has to
   be interface 0** — binding a different one leaves the device unusable and the driver will
   not find it.
3. The target driver on the right must be **`WinUSB`**. Choose it with the small **up/down
   arrows** beside that box. Do **not** pick `libusb-win32` or `libusbK`: they look like the
   right answer and they are not. (The "More Information" column on the far right is just
   links, not the selector.)
4. Press the large button. It may be labelled **`Install Driver`**, **`Replace Driver`** or
   **`Reinstall Driver`** depending on what is bound already — it is the same button either
   way.

The red cross beside **`WCID`** means nothing here. Ignore it.

Zadig installs a self-signed certificate into your Trusted Root store to sign the driver
package it generates, and this project deliberately **does not automate it** — automating it
would not remove that certificate installation, it would only remove your chance to notice
it. See [The goal is one installer](#the-goal-is-one-installer).

**If you ever need to undo it:** Device Manager → find the device → uninstall it and tick
"delete the driver software" → unplug and replug.

### Step 2 — Run the installer

`BCD3000Setup.exe`. It asks for administrator rights, shows you what it found on your
machine **before** changing anything, and lists what is left to do afterwards.

**Nothing this installer does requires restarting Windows.** There is no kernel component and
nothing loads at boot: an ASIO driver is a COM DLL that your DJ software loads when it opens
the sound card.

It installs:

```
%ProgramFiles%\BCD3000 ASIO Driver\      the ASIO driver, the uninstaller, a text manifest
%LOCALAPPDATA%\BCD3000Bridge\            the control and LED service, and its log
Startup\BCD3000 Bridge.lnk               starts the service when you sign in
```

**What it refuses to do**, on purpose: it never rebinds a USB device, it never creates a MIDI
port itself — not even to test one, it never turns off a Windows protection and never asks
you to, and it does not stop the control service while it is running.

### Step 3 — Start the control service

Sign out and back in, **or** double-click the shortcut in your Startup folder. The installer
tells you where it is.

Start it from Explorer, **not** from an administrator prompt: it has to run unelevated for
the driver to be able to reach it.

**Once it's running, it creates one virtual MIDI port named `BCD3000`** — how DJ software
sees the knobs, buttons and LEDs. It does this by loading **`BcdMidi.dll`** (ours), which
calls **Microsoft's Windows MIDI Services** through **`Windows.Devices.Midi2.dll`** —
Microsoft's own runtime, shipped beside the control service under the MIT licence (see
[Licence](#licence) below). Nothing third party is installed for this step any more: Windows
MIDI Services is part of Windows 11 itself. **Audio works without this service running; the
knobs, buttons and LEDs do not.**

A known Windows defect can stop this port from being created again until the machine
reboots — tracked upstream as `microsoft/MIDI` issue #1047, acknowledged by Microsoft with no
fix date. The installer's own screen 3 reads whether your Windows build is on the affected
list and says so plainly; **it never creates a port itself to find out whether yours will
work, and this README does not promise more than that.**

### Step 4 — Open your DJ software and select the driver

Pick **`Behringer BCD3000`** as the ASIO device. You should see 4 outputs and 4 inputs at
44.1 kHz.

> ### ⚠️ Do NOT let your DJ software install a driver for this mixer
>
> VirtualDJ **recognises** the BCD3000 — it shows the mixer, the mapping editor and every
> control — and **still** puts a band in its CONTROLLER tab saying drivers have to be
> installed first, with a **`Download drivers`** button beside it. In Portuguese the band
> reads *"Você precisa instalar alguns drivers primeiro"*.
>
> **That band is wrong here.** It is a fixed property of VirtualDJ's own controller
> definition, written when the manufacturer's package was the only way to use this mixer. It
> is not looking at your machine and nothing on your machine is missing.
>
> Pressing that button installs the manufacturer's 2010 INF files for
> `USB\VID_1397&PID_00BF`. Windows matches those over the WinUSB binding this driver needs,
> and then two things die at once: the old kernel driver refuses to load (code 39, expired
> signature) **and** the mixer is no longer bound to WinUSB. No audio **and** no controls. The
> way back out is running Zadig again.

### If the mixer "disappears"

**Check the cable first.** A plug that was not fully seated cost an hour of investigation on
the development machine, after two confident wrong theories. Push it home at both ends.

Second suspect: a virtual machine. From outside there is no way to tell "another program has
it" from "it is unplugged".

Only after both of those should you suspect the WinUSB binding. **Zadig is the last thing to
try, never the first guess.**

### Do not stop or restart the control service as a routine step

`BCD3000Bridge.exe` owns the virtual MIDI port for as long as it runs. Ending it destroys the
port, and any DJ application that had the controller open then has to be restarted before it
finds a new one. This is a declared limit of how the port is created. Leave it running — it
starts by itself at every sign-in, and it hands the device to the driver and takes it back on
its own.

---

## The goal is one installer

**One click was the goal from the start, and this is not it yet — but one of the two manual
steps that used to be here is gone.** Five steps remain above, and one of them is somebody
else's program. The reason it is still manual is worth stating plainly, because it is not
laziness:

* **Zadig.** This is not a licence problem and it is **deliberate**. Zadig's underlying
  library installs a self-signed certificate into the Trusted Root and Trusted Publishers
  stores *and* silently sets a Group Policy. Automating that would contradict the one sentence
  that justifies this project existing — *without turning off any Windows protection*. A
  command line for it exists, and that is exactly why it is tempting and wrong.
  The legitimate alternative is signing our own driver package, which needs a paid EV
  certificate and a company account, so it is out.

Nothing about this project's own code depends on Zadig specifically. It depends on the device
being bound to WinUSB and on one registry value being present. Any route that achieves those
two things works.

**What is not one-click, honestly:** Zadig's manual run above, and the unsigned-binary
SmartScreen warning described in [Installing it](#installing-it). Both stay manual for the
reasons stated there, not for lack of trying.

### What used to be here: loopMIDI / teVirtualMIDI

Until August 2026 there was a **second** manual step: installing **loopMIDI**, which brought
**teVirtualMIDI** with it, a third-party kernel-mode driver that created the virtual MIDI
port. That step is gone, and the reason is worth recording rather than deleting, because it
is also the reason this project could be published at all.

Embedding teVirtualMIDI's installer was **prohibited by its licence**, in two independent
places — the author's own software page, and the EULA inside the installer itself
("Distribution in any form without prior written permission by the author is prohibited!").
That included putting it in this repository, because a public repository is distribution. An
email asking the author about a redistributable licence, sent 2026-07-29, went unanswered.
That single clause was the thing blocking this project from being published at all — not a
technical problem, a legal one.

So the port now comes from **Windows MIDI Services** instead: Microsoft's own, MIT-licensed,
and already part of Windows 11. Microsoft's own FAQ answers the same licensing question the
opposite way: *"Q: Can I sell an application which uses Windows MIDI Services / A: Yes. Of
course."* The control service loads our own `BcdMidi.dll`, which calls Microsoft's
`Windows.Devices.Midi2.dll` — shipped beside it, under the MIT licence (see
[Licence](#licence) below, and the mechanism under Step 3 in
[Installing it](#installing-it)). **Nothing of Tobias Erichsen's software is loaded, linked
to, or called by this product any more.** Anyone who already has loopMIDI installed — from an
older version of this project, or for an unrelated reason — is unaffected: this project no
longer looks for it, either to use it or to remove it.

This change did not make the driver Windows-10-capable — if anything it is the opposite, see
[Windows 10](#windows-10) below — and it did not make the MIDI port's creation guaranteed to
work on every machine: see the known Windows defect noted in
[Status](#status) above and what Step 3 checks for it.

---

## Windows 10

The claim that used to be here — "it should work, nobody has tested it" — was written for
the **teVirtualMIDI** architecture, a kernel driver with no Windows 11 requirement of its own.
That architecture is gone (see [The goal is one installer](#the-goal-is-one-installer)), and
the claim does not automatically carry over to what replaced it.

**What is established, from Microsoft's own documentation, and not from a test on this
project's own hardware:** Windows MIDI Services requires Windows 11. Microsoft's own page for
it states plainly, *"The Windows MIDI Services runtime and tools requires Windows 11 with the
Windows MIDI Service, plugins, and USB driver pre-installed from Microsoft."* Nothing in this
project has verified that on real Windows 10 hardware, and there was no cheap way to do so
without a Windows 10 machine — but Microsoft's own statement is reason enough not to expect
the control service's MIDI port to work there.

**The audio path is a separate question, and the old reasoning about it still holds.** The
ASIO driver talks to the hardware over WinUSB's isochronous API, which has existed since
Windows 8.1, and nothing in this project's own code declares a minimum Windows version for
it. Nobody has tested it on Windows 10. So on Windows 10 the audio may still work; the knobs,
buttons and LEDs will not, because Windows MIDI Services is Windows 11 only.

**You may not need any of this on Windows 10 in the first place.** The trust removal that
breaks the manufacturer's 2010 driver is documented for Windows 11 24H2, 25H2, 26H1 and
Windows Server 2025 — **Windows 10 is not in that list.** So the manufacturer's own package
may still load for you, which is less work than this. Note that the two are mutually
exclusive: installing theirs is exactly the warning in Step 4.

This is out of scope for this project on purpose — the owner has called Windows 10 support
"a plus, not a requirement" — so none of the above blocks anything. If you try either on
Windows 10, please open an issue and say what happened. It decides who this project is for.

---

## Building it

Needs Visual Studio Build Tools (MSVC, x64) and, for the control service, Python with
PyInstaller. Nothing else — **no Inno Setup, no NSIS, no WiX, no packaging tool of any kind**,
by the same reasoning that keeps third party code out of the driver.

```bat
rem the driver
cd native\bcdasio
build.bat dll

rem the MIDI port, and Microsoft's runtime DLL copied beside it
cd ..\bcdmidi
build.bat dll

rem the control service. BUILD IT FROM THE .spec, never from a bare command
rem line: `pyinstaller --onefile ... bridge_service.py` OVERWRITES the .spec
rem with a generated one, and the generated one does not carry the two MIDI
rem DLLs. The build still succeeds and the MIDI port simply never appears.
cd ..\..\poc
pyinstaller --noconfirm BCD3000Bridge.spec

rem the installer, which embeds both of the above plus the uninstaller
cd ..\installer
build.bat all
```

Other `build.bat` targets in `native\bcdasio`: `tests`, `probe`, `testaudio`, `strict`
(`/W4 /WX`), `abicheck` (proves our ASIO interface declarations are binary-identical to the
Steinberg SDK's), `regcheck` (proves our registry writer produces the same tree the SDK's
did).

The **Steinberg ASIO SDK is not in this repository** and is not needed to build the driver —
the interface is declared in `native/bcdasio/asioapi.h`, and the ABI equivalence is proved by
the `abicheck` target when the SDK is present on disk. Download it from
[Steinberg](https://www.steinberg.net/developers/) if you want to run that proof.

---

## Licence

**MIT** — see [LICENSE](LICENSE).

Two things are **not** covered by it:

* `docs/BCD3000.PNG` is a manufacturer product photograph, used to show which device this is
  for. It is not ours and not MIT.
* Part of the driver derives from Steinberg's ASIO sample code, which is **BSD 3-clause**.
  That notice is preserved in `native/bcdasio/LICENSE-asiosample.txt`.

The **ASIO logo is deliberately not used anywhere** — it has its own trademark rules. "ASIO"
is a trademark of Steinberg Media Technologies GmbH.

One thing **is** redistributed, and its notice travels with it:

* `Windows.Devices.Midi2.dll` — Windows MIDI Services, from
  [github.com/microsoft/MIDI](https://github.com/microsoft/MIDI), under the **MIT** licence,
  **Copyright (c) Microsoft Corporation**. The installer embeds it and writes it beside the
  control service, so the MIT licence requires its copyright notice to be included with every
  copy. It is reproduced in full in [LICENSE](LICENSE), under *Third party software
  redistributed by this project*. The binary is not in this repository — `.gitignore` excludes
  `*.dll` — so a clone fetches the `Windows.Devices.Midi2` package from nuget.org, the same
  arrangement the ASIO SDK gets.

---

## Credits

Built by **MatroX**, an independent Brazilian developer, for one mixer that deserved better
than a driver that stopped loading.

Every measurement in this README came off real hardware. Several conclusions in it exist
because a test on that hardware proved an earlier theory wrong.
