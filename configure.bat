@ECHO off
SETLOCAL ENABLEDELAYEDEXPANSION
SETLOCAL ENABLEEXTENSIONS

IF "%VisualStudioVersion%" == "14.0" ( IF NOT DEFINED Platform SET "Platform=X86" )
FOR /F %%i IN ('powershell -c "\"%Platform%\".toLower()"') DO SET PLATFORM=%%i
powershell -c "if ('%PLATFORM%' -notin ('x86', 'x64')) {Exit 1}"
IF !ERRORLEVEL! NEQ 0 (
    ECHO Unknown platform: %PLATFORM%
    EXIT /B 1
)

SET "R2DIST=r2_dist"
SET "BUILDDIR=build_%PLATFORM%"


ECHO Prepare translations
IF NOT EXIST "src\translations\NUL" (
    git clone https://github.com/radareorg/iaito-translations.git src/translations
)
FOR %%i in (src\translations\*.ts) DO lrelease %%i

ECHO Preparing directory
CD %BUILDDIR%

IF NOT DEFINED IAITO_ENABLE_CRASH_REPORTS (
SET "IAITO_ENABLE_CRASH_REPORTS=false"
)

ECHO Building iaito
qmake BREAKPAD_SOURCE_DIR=%BREAKPAD_SOURCE_DIR% IAITO_ENABLE_CRASH_REPORTS=%IAITO_ENABLE_CRASH_REPORTS% %* ..\src\iaito.pro -config release
IF !ERRORLEVEL! NEQ 0 EXIT /B 1
