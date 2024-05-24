SET "R2DIR=radare2"
SET "BUILDDIR=build_%PLATFORM%"

ECHO Preparing directory
CD src
meson --buildtype=release ..\%BUILDDIR%
CD ..\%BUILDDIR%
ninja -j4
if %ERRORLEVEL% NEQ 0 (
	EXIT /B 1
)
CD ..

ECHO Deploying iaito
IF NOT EXIST release (
	MKDIR release
)
COPY %BUILDDIR%\iaito.exe release\iaito.exe
XCOPY /S /I %R2DIR%\share release\share
XCOPY /S /I %R2DIR%\lib release\lib
DEL release\lib\*.lib
COPY %R2DIR%\bin\*.dll release\
windeployqt release\iaito.exe
FOR %%i in (..\src\translations\*.qm) DO MOVE "%%~fi" release\translations

IF DEFINED CI (
	RENAME release iaito
)

ENDLOCAL
