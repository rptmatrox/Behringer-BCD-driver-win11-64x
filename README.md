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

**Right now this is six steps, and two of them are other people's programs.** That is the
honest answer, and shortening it is the project's main open problem — see
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

### Step 1 — Install **loopMIDI**, which brings **teVirtualMIDI** with it

* Download page: **https://www.tobias-erichsen.de/software/loopMIDI.html**
* Verified version: **1.0.16.27**
* SHA-256: `975399EB76E2C5A1D553ECD8975AB361261CCAB8FD9412B8060B19877CB4E0D5`
* Also available as `winget install --id TobiasErichsen.loopMIDI`

To check what you downloaded: `certutil -hashfile loopMIDISetup.exe SHA256`

**This is somebody else's software and it is not part of this project.** It is by Tobias
Erichsen, it is not redistributed here, and it is not in this repository — its licence does
not permit that. The control service loads `teVirtualMIDI64.dll` at run time to create one
virtual MIDI port named `BCD3000`, which is how DJ software sees the controller. **Audio
works without it; the knobs, buttons and LEDs do not.**

It contains a kernel-mode driver and installs it properly through PnP, so **this is the step
that may ask you to restart Windows.** That is why it comes first.

We are not the source of those bytes, but we are a source of truth about which bytes are the
right ones — hence the version and hash above.

### Step 2 — Run **Zadig** once, to bind the device to WinUSB

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

### Step 3 — Run the installer

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

**What it refuses to do**, on purpose: it never rebinds a USB device, it never installs
teVirtualMIDI, it never turns off a Windows protection and never asks you to, and it does not
stop the control service while it is running.

### Step 4 — Start the control service

Sign out and back in, **or** double-click the shortcut in your Startup folder. The installer
tells you where it is.

Start it from Explorer, **not** from an administrator prompt: it has to run unelevated for
the driver to be able to reach it.

### Step 5 — Open your DJ software and select the driver

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

**One click was the goal from the start, and this is not it yet.** Two of the six steps above
are somebody else's program, and the reasons each one is still manual are worth stating
plainly, because they are not laziness:

* **loopMIDI / teVirtualMIDI.** Embedding its installer is **prohibited by its licence**, in
  two independent places — the author's software page, and the EULA inside the installer
  itself ("Distribution in any form without prior written permission by the author is
  prohibited!"). That includes putting it in this repository, because a public repository is
  distribution. So the installer detects it, explains it, and points at the author's own
  download page. A redistributable module **does** exist for licensees, which would collapse
  this step to zero — that conversation is open.
* **Zadig.** This one is not a licence problem and it is **deliberate**. Zadig's underlying
  library installs a self-signed certificate into the Trusted Root and Trusted Publishers
  stores *and* silently sets a Group Policy. Automating that would contradict the one sentence
  that justifies this project existing — *without turning off any Windows protection*. A
  command line for it exists, and that is exactly why it is tempting and wrong.
  The legitimate alternative is signing our own driver package, which needs a paid EV
  certificate and a company account, so it is out.

Nothing about this project's own code depends on Zadig specifically. It depends on the device
being bound to WinUSB and on one registry value being present. Any route that achieves those
two things works.

The direction that would remove the teVirtualMIDI dependency entirely is **Windows MIDI
Services** (MIT-licensed, user mode). It is not usable yet: Windows 11 only, its MIDI 1.0
loopback still needs Developer Mode, and its preview binaries say not to ship them. It is
being watched.

---

## Windows 10

**It should work, and nobody has tested it.** That is the honest statement, and the README is
not going to claim more than that. Nothing declares a minimum Windows version anywhere in the
code, and the real floor is WinUSB's isochronous API, which has existed since Windows 8.1.

**But you may not need this project at all on Windows 10.** The trust removal that breaks the
2010 driver is documented for Windows 11 24H2, 25H2, 26H1 and Windows Server 2025 — **Windows
10 is not in that list.** So the manufacturer's own package may still load for you, which is
less work than this. Note that the two are mutually exclusive: installing theirs is exactly
the warning in Step 5.

If you try either on Windows 10, please open an issue and say what happened. It decides who
this project is for.

---

## Building it

Needs Visual Studio Build Tools (MSVC, x64) and, for the control service, Python with
PyInstaller. Nothing else — **no Inno Setup, no NSIS, no WiX, no packaging tool of any kind**,
by the same reasoning that keeps third party code out of the driver.

```bat
rem the driver
cd native\bcdasio
build.bat dll

rem the control service
cd ..\..\poc
pyinstaller --onefile --noconsole --name BCD3000Bridge bridge_service.py

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

`teVirtualMIDI` is copyright Tobias Erichsen. It is a **run-time dependency loaded by the
control service** and it is **not redistributed here**.

---

## Credits

Built by **MatroX**, an independent Brazilian developer, for one mixer that deserved better
than a driver that stopped loading.

Every measurement in this README came off real hardware. Several conclusions in it exist
because a test on that hardware proved an earlier theory wrong.
