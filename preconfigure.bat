@ECHO OFF

IF NOT DEFINED R2V SET "R2V=5.8.8"
SET ARCH=x64

SETLOCAL ENABLEDELAYEDEXPANSION

IF "%VisualStudioVersion%" == "14.0" ( IF NOT DEFINED Platform SET "Platform=X86" )
FOR /F %%i IN ('powershell -c "\"%Platform%\".toLower()"') DO SET PLATFORM=%%i
powershell -c "if ('%PLATFORM%' -notin ('x86', 'x64')) {Exit 1}"
IF !ERRORLEVEL! NEQ 0 (
    ECHO Unknown platform: %PLATFORM%
    EXIT /B 1
)

SET "PATH=%CD%;%PATH%"
SET "R2DIR=radare2"

IF NOT DEFINED CI (
ECHO Downloading radare2 (%PLATFORM%)
curl -L -o r2.zip https://github.com/radareorg/radare2/releases/download/%R2V%/radare2-%R2V%-w64.zip
unzip r2.zip
RENAME radare2-%R2V%-w64 %R2DIR%
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" %ARCH%
)
SET "PATH=%CD%\%R2DIR%\bin;%PATH%"

rem ECHO Building radare2 (%PLATFORM%)
rem CD radare2
rem git clean -xfd
rem RMDIR /S /Q ..\%R2DIR%
rem meson -C src r2_builddir
rem meson.exe r2_builddir --buildtype=release --prefix=%CD%\..\%R2DIR% || EXIT /B 1
rem ninja -C r2_builddir install || EXIT /B 1
rem IF !ERRORLEVEL! NEQ 0 EXIT /B 1
