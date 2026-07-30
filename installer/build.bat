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
set CORELIBS=ole32.lib shell32.lib advapi32.lib setupapi.lib user32.lib gdi32.lib comctl32.lib msimg32.lib windowscodecs.lib

rem /MANIFEST:NO because the manifest is embedded through the .rc file. Without
rem this the linker adds a second one and the resource ids collide.
set LDFLAGS=/MANIFEST:NO

set ASIO_DLL=..\native\bcdasio\BcdAsio.dll
set BRIDGE_EXE=..\poc\dist\BCD3000Bridge.exe
set DEVICE_PHOTO=..\docs\BCD3000.PNG

if /I "%TARGET%"=="check"     goto :check
if /I "%TARGET%"=="uninstall" goto :uninstall
if /I "%TARGET%"=="setup"     goto :setup
if /I "%TARGET%"=="all"       goto :all
echo Unknown target: %TARGET%  (use check^|uninstall^|setup^|all)
exit /b 1

:all
call "%~f0" check     || exit /b 1
call "%~f0" setup     || exit /b 1
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
