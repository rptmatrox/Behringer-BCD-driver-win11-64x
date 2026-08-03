@echo off
rem Builds BcdMidi.dll - the product's virtual MIDI port, on Windows MIDI
rem Services - and its self-test, with the same MSVC that builds the driver.
rem
rem Targets:
rem   dll        BcdMidi.dll        the product. /W4 /WX, zero warnings.
rem   selftest   selftest.exe       console harness for the DLL. Never ships.
rem   all        dll, then selftest
rem
rem THE VERDICT OF EACH TARGET DEPENDS ON THE REAL EXIT CODE of cppwinrt and of
rem cl (which covers the link it invokes through /link) AND on the output file
rem existing. Both conditions together, never just one. Same discipline as
rem native/bcdasio/build.bat and installer/build.bat, and for the same measured
rem reason: a verdict taken from "if exist <file>" alone prints success when the
rem link failed and an old binary is still on disk.
rem
rem Always "if errorlevel 1" on a line of its own. %ERRORLEVEL% inside a
rem ( ... ) block is expanded when cmd READS the block, not when it runs it, and
rem gives the wrong answer.
rem
rem WHERE MICROSOFT'S PACKAGE LIVES. wms\ holds the three files taken from the
rem Windows.Devices.Midi2 NuGet package (MIT, github.com/microsoft/MIDI):
rem   Windows.Devices.Midi2.winmd   151,552 bytes   the metadata cppwinrt reads
rem   Windows.Devices.Midi2.dll   2,892,288 bytes   the runtime, copied beside
rem                                                 our binaries below
rem   Windows.Devices.Midi2.pri         712 bytes   its resources
rem The .winmd is versioned; the .dll and .pri are not, because .gitignore
rem excludes *.dll everywhere in this repository. Whoever clones this fetches
rem the package from nuget.org and drops those two files into wms\, the same
rem arrangement native/ASIOSDK/ already uses. Set BCDMIDI_WINMD to build against
rem a copy somewhere else instead.
rem
rem NO MANIFEST AND NO REGISTRATION. C++/WinRT falls back to DllGetActivationFactory
rem on a DLL sitting beside the executable, so Windows.Devices.Midi2.dll only has
rem to be in the process directory. Measured on 2026-08-01. That is also why the
rem copy below is part of the build and not an afterthought: without it the DLL
rem builds and links fine and every call into it fails at run time.

setlocal
cd /d "%~dp0"

if "%BCDMIDI_WINMD%"=="" set BCDMIDI_WINMD=wms\Windows.Devices.Midi2.winmd
set CPPWINRT="C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\cppwinrt.exe"
set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

set TARGET=%1
if "%TARGET%"=="" set TARGET=all

if /I "%TARGET%"=="dll"      goto :projection
if /I "%TARGET%"=="selftest" goto :projection
if /I "%TARGET%"=="all"      goto :projection
echo Unknown target: %TARGET%  (use dll^|selftest^|all)
exit /b 1

rem Stage one: the C++/WinRT projection. Regenerated every run - it is derived
rem from the .winmd and from the Windows SDK version, and a stale winrt\ built
rem from a different .winmd is exactly the kind of thing that compiles.
:projection
if not exist "%BCDMIDI_WINMD%" (
    echo ===== BUILD_FAIL: projection - metadata not found =====
    echo.
    echo Looked for: %BCDMIDI_WINMD%
    echo.
    echo This is Microsoft's Windows.Devices.Midi2 package ^(MIT^). Fetch
    echo Windows.Devices.Midi2 from nuget.org, and copy from inside it:
    echo   ref\native\Windows.Devices.Midi2.winmd           -^> wms\
    echo   runtimes\win-x64\native\Windows.Devices.Midi2.dll -^> wms\
    echo   runtimes\win-x64\native\Windows.Devices.Midi2.pri -^> wms\
    exit /b 1
)

%CPPWINRT% -input "%BCDMIDI_WINMD%" -input 10.0.26100.0 -output winrt
if errorlevel 1 ( echo ===== BUILD_FAIL: projection ===== & exit /b 1 )
if not exist winrt\winrt\Windows.Devices.Midi2.h ( echo ===== BUILD_FAIL: projection produced no header ===== & exit /b 1 )

call %VCVARS% >nul
if errorlevel 1 ( echo VCVARS_FAIL & exit /b 1 )

if /I "%TARGET%"=="selftest" goto :selftest
goto :dll

rem oleaut32.lib is not optional: without it the link fails with LNK2019 on
rem WINRT_IMPL_SysStringLen, which C++/WinRT's base.h reaches for.
rem /MANIFEST:NO because nothing here needs one - see the note above.
:dll
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX /MD /permissive- /Iwinrt /D_CRT_SECURE_NO_WARNINGS ^
   bcdmidi.cpp /LD /Fe:BcdMidi.dll /link /MANIFEST:NO runtimeobject.lib ole32.lib oleaut32.lib
if errorlevel 1          ( echo ===== BUILD_FAIL: dll ===== & exit /b 1 )
if not exist BcdMidi.dll ( echo ===== BUILD_FAIL: dll missing ===== & exit /b 1 )
if not exist BcdMidi.lib ( echo ===== BUILD_FAIL: dll produced no import library ===== & exit /b 1 )

copy /y wms\Windows.Devices.Midi2.dll . >nul
if errorlevel 1 ( echo ===== BUILD_FAIL: Microsoft runtime DLL not copied ===== & exit /b 1 )
if not exist Windows.Devices.Midi2.dll ( echo ===== BUILD_FAIL: Windows.Devices.Midi2.dll missing ===== & exit /b 1 )

rem The .pri gets the same two conditions as everything else. It had neither,
rem which made it the one step in this file that could vanish and still print
rem BUILD_OK - in a script whose own header says "both conditions together,
rem never just one".
copy /y wms\Windows.Devices.Midi2.pri . >nul
if errorlevel 1 ( echo ===== BUILD_FAIL: Microsoft resource file not copied ===== & exit /b 1 )
if not exist Windows.Devices.Midi2.pri ( echo ===== BUILD_FAIL: Windows.Devices.Midi2.pri missing ===== & exit /b 1 )

echo ===== BUILD_OK: BcdMidi.dll =====
if /I "%TARGET%"=="dll" goto :eof

rem The self-test links the import library, so the DLL target has to have run.
:selftest
if not exist BcdMidi.lib ( echo ===== BUILD_FAIL: selftest - build the dll target first ===== & exit /b 1 )
cl /nologo /W4 /WX /D_CRT_SECURE_NO_WARNINGS selftest.cpp /Fe:selftest.exe /link BcdMidi.lib winmm.lib
if errorlevel 1           ( echo ===== BUILD_FAIL: selftest ===== & exit /b 1 )
if not exist selftest.exe ( echo ===== BUILD_FAIL: selftest missing ===== & exit /b 1 )
echo ===== BUILD_OK: selftest.exe =====
goto :eof
