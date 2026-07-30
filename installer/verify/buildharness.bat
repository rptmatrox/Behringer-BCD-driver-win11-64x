@echo off
rem Builds the verification harness. NOT part of the product: it renders the
rem installer's pages on a private desktop and measures them, which is how a change
rem to the window is proved without running the installer.
rem
rem cd /d "%~dp0" first, then everything RELATIVE to here - the same discipline as
rem installer\build.bat and native\bcdasio\build.bat. INST used to be an absolute
rem path, left over from the harness having been written outside the repository
rem before it was committed. Two things were wrong with that once it was in git: it
rem carried the profile name of whoever wrote it, and it made the harness
rem unbuildable for anybody who clones - which is the same as not having a harness.
setlocal
cd /d "%~dp0"
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set INST=..
rc /nologo /fo bcdverify.res bcdverify.rc
if errorlevel 1 ( echo ===== HARNESS_FAIL: res ===== & exit /b 1 )
cl /EHsc /nologo /O2 /W4 /wd4505 /D_CRT_SECURE_NO_WARNINGS /DWIN32 /D_WINDOWS /I"%INST%" bcdverify.cpp "%INST%\common.cpp" /Fe:bcdverify.exe /link /MANIFEST:NO bcdverify.res ole32.lib shell32.lib advapi32.lib setupapi.lib user32.lib gdi32.lib comctl32.lib msimg32.lib windowscodecs.lib
if errorlevel 1 ( echo ===== HARNESS_FAIL: cl ===== & exit /b 1 )
if not exist bcdverify.exe ( echo ===== HARNESS_FAIL: missing ===== & exit /b 1 )
echo ===== HARNESS_OK =====
