SET "R2DIST=r2_dist"
SET "BUILDDIR=build_%PLATFORM%"

ECHO Preparing directory
rem RMDIR /S /Q %BUILDDIR%
rem MKDIR %BUILDDIR%
CD %BUILDDIR%
nmake
meson configure --buildtype=release ..\src
rem CD ..\release
ninja -v -j4
REM IF !ERRORLEVEL! NEQ 0 EXIT /B 1
CD ..

ECHO Deploying iaito
MKDIR iaito
COPY src\iaito.exe iaito\iaito.exe
XCOPY /S /I ..\%R2DIST%\share iaito\share
XCOPY /S /I ..\%R2DIST%\lib iaito\lib
DEL iaito\lib\*.lib
COPY ..\%R2DIST%\bin\*.dll iaito\
windeployqt iaito\iaito.exe
FOR %%i in (..\src\translations\*.qm) DO MOVE "%%~fi" iaito\translations

ENDLOCAL
