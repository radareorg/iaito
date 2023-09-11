SET "R2DIR=radare2"
SET "BUILDDIR=build_%PLATFORM%"

ECHO Preparing directory
IF EXIST %BUILDDIR% (
RMDIR /S /Q %BUILDDIR%
)

CD src && meson --buildtype=release ..\%BUILDDIR%
CD ..\%BUILDDIR% && ninja -j4
REM IF !ERRORLEVEL! NEQ 0 EXIT /B 1
CD ..

ECHO Deploying iaito
MKDIR iaito
COPY %BUILDDIR%\iaito.exe iaito\iaito.exe
RMDIR /S /Q %BUILDDIR%
XCOPY /S /I %R2DIR%\share iaito\share
XCOPY /S /I %R2DIR%\lib iaito\lib
DEL iaito\lib\*.lib
COPY %R2DIR%\bin\*.dll iaito\
windeployqt iaito\iaito.exe
FOR %%i in (..\src\translations\*.qm) DO MOVE "%%~fi" iaito\translations

ENDLOCAL
