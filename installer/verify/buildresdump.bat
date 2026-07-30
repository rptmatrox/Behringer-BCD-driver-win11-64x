@echo off
setlocal
cd /d "%~dp0"
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cl /EHsc /nologo /O2 /W4 /D_CRT_SECURE_NO_WARNINGS resdump.cpp /Fe:resdump.exe /link /MANIFEST:NO user32.lib
if errorlevel 1 ( echo RESDUMP_FAIL & exit /b 1 )
echo RESDUMP_OK
