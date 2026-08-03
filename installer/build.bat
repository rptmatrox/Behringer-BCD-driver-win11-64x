@echo off
rem Builds the installer, the uninstaller and the read only checker with the same
rem MSVC that builds the driver. No packaging tool is involved and none is needed.
rem
rem Targets:
rem   check      BCD3000Check.exe      read only, no elevation, no payloads
rem   uninstall  BCD3000Uninstall.exe  no payloads
rem   setup      BCD3000Setup.exe      embeds the driver, the service and the uninstaller.
rem              BUILDS THE UNINSTALLER FIRST, always: it is embedded as a resource,
rem              so a stale BCD3000Uninstall.exe on disk would be shipped inside a
rem              freshly built installer, and the "if exist" check below would be
rem              perfectly happy about it. Relying on the caller to run the targets
rem              in the right order is the same class of defect as taking a build
rem              verdict from a file's existence.
rem   all        check, then setup (which builds the uninstaller on the way)
rem   strict     compile only, /W4 /WX, every source in this folder plus the harness.
rem              Not part of `all` and it produces no binary. See the block at :strict.
rem
rem THE VERDICT OF EACH TARGET DEPENDS ON THE REAL EXIT CODE of rc and of cl (which
rem covers the link it invokes through /link) AND on the output file existing. Both
rem conditions together, never just one. This is the same discipline as
rem native/bcdasio/build.bat, and for the same measured reason: a verdict taken
rem from "if exist <file>" alone prints success when the link failed and an old
rem binary is still on disk.
rem
rem Always use "if errorlevel 1" on a line of its own. %ERRORLEVEL% inside a
rem ( ... ) block is expanded when cmd reads the block, not when it runs it, and
rem gives the wrong answer. Measured, not assumed.

setlocal
cd /d "%~dp0"

call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 ( echo VCVARS_FAIL & exit /b 1 )

set TARGET=%1
if "%TARGET%"=="" set TARGET=all

set CFLAGS=/EHsc /nologo /O2 /W4 /D_CRT_SECURE_NO_WARNINGS /DWIN32 /D_WINDOWS /I.

rem Every one of these is a Windows component. There is still no third party
rem dependency anywhere in this build - which is the point, and is why the window
rem is hand written USER32 rather than a packaging tool's dialog:
rem   gdi32        the drawing
rem   comctl32     the progress bar and the version 6 button look
rem   msimg32      AlphaBlend, for the device photograph's transparency
rem   windowscodecs WIC, which decodes the PNG at run time
rem   version      GetFileVersionInfoW, which reads the Windows MIDI service's own
rem                build number out of its resource section without loading it
set CORELIBS=ole32.lib shell32.lib advapi32.lib setupapi.lib user32.lib gdi32.lib comctl32.lib msimg32.lib windowscodecs.lib version.lib

rem /MANIFEST:NO because the manifest is embedded through the .rc file. Without
rem this the linker adds a second one and the resource ids collide.
set LDFLAGS=/MANIFEST:NO

set ASIO_DLL=..\native\bcdasio\BcdAsio.dll
set BRIDGE_EXE=..\poc\dist\BCD3000Bridge.exe
set MIDI_DLL=..\native\bcdmidi\BcdMidi.dll
set WINMIDI_DLL=..\native\bcdmidi\wms\Windows.Devices.Midi2.dll
set BCDMIDI_SRC=..\native\bcdmidi\bcdmidi.cpp
set BCDMIDI_PROJ=..\native\bcdmidi\winrt
set DEVICE_PHOTO=..\docs\BCD3000.PNG
set ZADIG_SHOT=..\docs\Zadig.png

if /I "%TARGET%"=="check"     goto :check
if /I "%TARGET%"=="uninstall" goto :uninstall
if /I "%TARGET%"=="setup"     goto :setup
if /I "%TARGET%"=="strict"    goto :strict
if /I "%TARGET%"=="all"       goto :all
echo Unknown target: %TARGET%  (use check^|uninstall^|setup^|strict^|all)
exit /b 1

:all
call "%~f0" check     || exit /b 1
call "%~f0" setup     || exit /b 1
goto :eof

rem The `strict` target exists because this script has already learned, the expensive
rem way, that a verdict which depends on somebody READING THE SCREEN is not a verdict.
rem The comment at the top tells the old version of that lesson (the BUILD_OK that came
rem out of an "if exist" and lied); the new version of it is the reason this target
rem exists: in the round that added the uninstaller's /preview, cl warned C4429 once and
rem C4129 four times about a malformed escape sequence in the harness, the warnings
rem scrolled past, and the defect shipped. Under /WX that would have been BUILD_FAIL and
rem exit code 1, and it would not have shipped.
rem
rem This target is native\bcdasio\build.bat :strict's pattern, and installer\ was the
rem last corner of the project without it.
rem
rem Three decisions, each with a reason:
rem  1. /WX HERE AND NOWHERE ELSE. The targets above stay at CFLAGS' default warning
rem     level on purpose: a new compiler warning must not stop somebody producing a
rem     binary to test on the hardware. This target is where a warning becomes an error.
rem  2. ALL OF OUR OWN UNITS: the five product ones (check.cpp, uninstall.cpp,
rem     setup.cpp, common.cpp, gui.cpp), the verification harness verify\bcdverify.cpp,
rem     and the virtual MIDI port ..\native\bcdmidi\bcdmidi.cpp. 5 + 1 + 1 = 7, which is
rem     the number the BUILD_OK line prints.
rem     The harness IS included, because the harness is where the defect above happened;
rem     leaving it out would leave out the one file that motivated the target.
rem     It is THREE cl calls and not one, and each split has its own reason.
rem       - /wd4505 for the harness: bcdverify.cpp includes the whole of setup.cpp and
rem         uninstall.cpp, so it sees the `static` functions those units do not use, and
rem         that is a property of the harness rather than of the sources. Giving /wd4505
rem         to the five above would hide a genuinely dead function in the product.
rem       - bcdmidi.cpp is C++/WinRT: it needs /std:c++20, /permissive- and the generated
rem         projection on the include path, none of which the units above want. It is
rem         compiled here anyway because it is OURS and it is the file this whole
rem         migration is built on; a source nothing compiles under /WX is a source whose
rem         warnings nobody sees.
rem  3. COMPILE ONLY (/c), with the .obj files in a directory of their own (strict\).
rem     So this target replaces no binary that may be in use and no .obj belonging to the
rem     targets above, and it needs no payload: it runs on a machine with no BcdAsio.dll,
rem     no BCD3000Bridge.exe and no photograph of the device, all of which the `setup`
rem     and `all` targets require. It needs no .res either, because it links nothing.
rem
rem *** WHAT THE SEVENTH UNIT COSTS, SAID HERE RATHER THAN LEFT TO BE DISCOVERED. ***
rem This used to be "the one target in this script that a freshly cloned machine can
rem run", and bcdmidi.cpp ends that: it needs the C++/WinRT projection, which is
rem generated by native\bcdmidi\build.bat from Microsoft's .winmd. So the projection is
rem CHECKED FOR, with the one command that produces it, rather than left to cl's
rem "cannot open include file" - which is the same discipline the payload checks in
rem :setup already use. Skipping the unit when the projection is absent was the other
rem candidate and is worse: a target that quietly compiles six units and still prints
rem seven is precisely the class of verdict this whole script exists to stop.
:strict
if not exist strict mkdir strict
if not exist "%BCDMIDI_PROJ%\winrt\Windows.Devices.Midi2.h" (
  echo ===== BUILD_FAIL: strict =====
  echo Missing the C++/WinRT projection: %BCDMIDI_PROJ%\winrt\Windows.Devices.Midi2.h
  echo.
  echo bcdmidi.cpp is the seventh unit of this target and it is generated against
  echo Microsoft's metadata. Produce it first:
  echo   cd ..\native\bcdmidi ^&^& build.bat dll
  echo That target says what to fetch from nuget.org if the metadata is missing too.
  exit /b 1
)
cl /c /W4 /WX %CFLAGS% /Fostrict\ check.cpp uninstall.cpp setup.cpp common.cpp gui.cpp
if errorlevel 1 ( echo ===== BUILD_FAIL: strict ===== & exit /b 1 )
cl /c /W4 /WX /wd4505 %CFLAGS% /Fostrict\ verify\bcdverify.cpp
if errorlevel 1 ( echo ===== BUILD_FAIL: strict ===== & exit /b 1 )
rem The same flags native\bcdmidi\build.bat compiles it with, minus the linking half.
cl /c /std:c++20 /EHsc /O2 /W4 /WX /MD /permissive- /nologo /D_CRT_SECURE_NO_WARNINGS ^
   /I"%BCDMIDI_PROJ%" /Fostrict\ "%BCDMIDI_SRC%"
if errorlevel 1 ( echo ===== BUILD_FAIL: strict ===== & exit /b 1 )
echo ===== BUILD_OK: strict (7 units at /W4 /WX, zero warnings) =====
goto :eof

:check
rc /nologo /fo check.res check.rc
if errorlevel 1 ( echo ===== BUILD_FAIL: check.res ===== & exit /b 1 )
cl %CFLAGS% check.cpp common.cpp /Fe:BCD3000Check.exe /link %LDFLAGS% check.res %CORELIBS%
if errorlevel 1             ( echo ===== BUILD_FAIL: check ===== & exit /b 1 )
if not exist BCD3000Check.exe ( echo ===== BUILD_FAIL: check ===== & exit /b 1 )
echo ===== BUILD_OK: BCD3000Check.exe =====
goto :eof

:uninstall
rc /nologo /fo uninstall.res uninstall.rc
if errorlevel 1 ( echo ===== BUILD_FAIL: uninstall.res ===== & exit /b 1 )
cl %CFLAGS% uninstall.cpp common.cpp gui.cpp /Fe:BCD3000Uninstall.exe /link %LDFLAGS% uninstall.res %CORELIBS%
if errorlevel 1                   ( echo ===== BUILD_FAIL: uninstall ===== & exit /b 1 )
if not exist BCD3000Uninstall.exe ( echo ===== BUILD_FAIL: uninstall ===== & exit /b 1 )
echo ===== BUILD_OK: BCD3000Uninstall.exe =====
goto :eof

:setup
rem The payloads are checked here rather than left to rc, which reports a missing
rem payload as a generic "cannot open file" that reads like a broken toolchain.
if not exist "%ASIO_DLL%" (
  echo ===== BUILD_FAIL: setup =====
  echo Missing payload: %ASIO_DLL%
  echo Build it first:  cd ..\native\bcdasio ^&^& build.bat dll
  exit /b 1
)
if not exist "%BRIDGE_EXE%" (
  echo ===== BUILD_FAIL: setup =====
  echo Missing payload: %BRIDGE_EXE%
  echo Build it first with PyInstaller, from poc\: see installer\README.md
  exit /b 1
)
rem The two DLLs that go BESIDE the control service. They are a pair: ours reaches
rem Windows MIDI Services through Microsoft's runtime, and shipping one without the
rem other produces a control service that starts and then fails at the first MIDI
rem call - which looks like a working install, so it is checked here.
if not exist "%MIDI_DLL%" (
  echo ===== BUILD_FAIL: setup =====
  echo Missing payload: %MIDI_DLL%
  echo Build it first:  cd ..\native\bcdmidi ^&^& build.bat dll
  exit /b 1
)
rem MICROSOFT'S RUNTIME, AND IT IS NOT A BUILD OUTPUT - it comes out of a NuGet
rem package. It is not in this repository because .gitignore excludes *.dll
rem everywhere here, the same arrangement native\ASIOSDK\ gets, so a missing file
rem means "fetch the package", not "run a build". The message says which one and
rem which architecture: the arm64 build has the SAME NAME and is 6,111,744 bytes.
rem
rem It is embedded under the MIT licence, Copyright (c) Microsoft Corporation. See
rem the repository's LICENSE, and setup.rc's resource 105.
if not exist "%WINMIDI_DLL%" (
  echo ===== BUILD_FAIL: setup =====
  echo Missing payload: %WINMIDI_DLL%
  echo.
  echo That is Microsoft's Windows MIDI Services runtime ^(MIT^). It is not a build
  echo output and it is deliberately not in this repository. Fetch the
  echo Windows.Devices.Midi2 package from nuget.org and copy, from inside it:
  echo   runtimes\win-x64\native\Windows.Devices.Midi2.dll -^> native\bcdmidi\wms\
  echo Take the win-x64 one: it is 2,892,288 bytes. The arm64 build has the same
  echo name, is 6,111,744 bytes, and must not be shipped.
  exit /b 1
)
rem The picture on the window's first page.
rem
rem IT IS NOT IN THE REPOSITORY, and that is on purpose: it is a manufacturer
rem product photograph, it is not covered by this project's licence, and this
rem project already keeps third party material it may not redistribute on the disk
rem instead of in the tree - see the ASIOSDK and "Drivers originais" entries in
rem .gitignore. So it is checked here, with the sentence that says what to do,
rem rather than being left to rc's generic "cannot open file".
if not exist "%DEVICE_PHOTO%" (
  echo ===== BUILD_FAIL: setup =====
  echo Missing resource: %DEVICE_PHOTO%
  echo.
  echo That is the picture on the installer window's first page. It is a
  echo manufacturer product photograph and is deliberately NOT in this repository.
  echo Put a 32 bit PNG with an alpha channel there, or point setup.rc's resource
  echo 110 somewhere else - it is ONE file and ONE id, so swapping it is one edit.
  echo See installer\README.md, "The device photograph".
  exit /b 1
)
rem The Zadig screenshot on the checks page.
rem
rem UNLIKE THE PHOTOGRAPH ABOVE, THIS ONE IS IN THE REPOSITORY, so a missing file
rem here means a broken checkout rather than a step nobody has done yet - and the
rem message says so, because the two situations need opposite actions.
if not exist "%ZADIG_SHOT%" (
  echo ===== BUILD_FAIL: setup =====
  echo Missing resource: %ZADIG_SHOT%
  echo.
  echo That is the screenshot of Zadig's window on the checks page, and it IS
  echo tracked in this repository - so this is a broken checkout, not a missing
  echo step. Restore it with: git checkout -- docs/Zadig.png
  echo See installer\README.md, "The Zadig screenshot".
  exit /b 1
)
rem The uninstaller is a payload of this target, so it is built here rather than
rem assumed to be current. The "if exist" below stays as the verification.
call "%~f0" uninstall || exit /b 1
if not exist BCD3000Uninstall.exe (
  echo ===== BUILD_FAIL: setup =====
  echo Missing payload: BCD3000Uninstall.exe
  exit /b 1
)
rc /nologo /fo setup.res setup.rc
if errorlevel 1 ( echo ===== BUILD_FAIL: setup.res ===== & exit /b 1 )
cl %CFLAGS% setup.cpp common.cpp gui.cpp /Fe:BCD3000Setup.exe /link %LDFLAGS% setup.res %CORELIBS%
if errorlevel 1               ( echo ===== BUILD_FAIL: setup ===== & exit /b 1 )
if not exist BCD3000Setup.exe ( echo ===== BUILD_FAIL: setup ===== & exit /b 1 )
echo ===== BUILD_OK: BCD3000Setup.exe =====
goto :eof
