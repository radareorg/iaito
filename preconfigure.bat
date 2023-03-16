@ECHO OFF

SET "R2V=5.8.4"
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
SET "R2DIST=r2_dist"

ECHO Downloading radare2 (%PLATFORM%)
rem powershell -command "Invoke-WebRequest 'https://github.com/radareorg/radare2/releases/download/5.1.0/radare2-5.1.0_windows.zip' -OutFile 'radare2-5.1.0_windows.zip'"
pip install wget
python -m wget -o r2.zip https://github.com/radareorg/radare2/releases/download/%R2V%/radare2-%R2V%-w64.zip
unzip r2.zip
RENAME radare2-%R2V%-w64 radare2
RMDIR /S /Q %R2DIST%
MOVE radare2-%R2V%-w64 %R2DIST%
SET "PATH=%CD%\%R2DIST%\bin;%PATH%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" %ARCH%

rem ECHO Building radare2 (%PLATFORM%)
rem CD radare2
rem git clean -xfd
rem RMDIR /S /Q ..\%R2DIST%
rem meson -C src r2_builddir
rem meson.exe r2_builddir --buildtype=release --prefix=%CD%\..\%R2DIST% || EXIT /B 1
rem ninja -C r2_builddir install || EXIT /B 1
rem IF !ERRORLEVEL! NEQ 0 EXIT /B 1
