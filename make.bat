SET "R2DIST=r2_dist"
SET "BUILDDIR=build_%PLATFORM%"

ECHO Preparing directory
RMDIR /S /Q %BUILDDIR%
MKDIR %BUILDDIR%
REM CD release
REM meson ..\release
REM CD ..\release
REM ninja
nmake
IF !ERRORLEVEL! NEQ 0 EXIT /B 1

ECHO Deploying iaito
MKDIR iaito
COPY release\iaito.exe iaito\iaito.exe
XCOPY /S /I ..\%R2DIST%\share iaito\share
XCOPY /S /I ..\%R2DIST%\lib iaito\lib
DEL iaito\lib\*.lib
COPY ..\%R2DIST%\bin\*.dll iaito\
windeployqt iaito\iaito.exe
FOR %%i in (..\src\translations\*.qm) DO MOVE "%%~fi" iaito\translations

ENDLOCAL
