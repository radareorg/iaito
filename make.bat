SET "R2DIST=r2_dist"
SET "BUILDDIR=build_%PLATFORM%"

ECHO Preparing directory
rem RMDIR /S /Q %BUILDDIR%
rem MKDIR %BUILDDIR%
rem CD %BUILDDIR%
rem nmake
rem meson configure --buildtype=release ..\src
rem CD ..\release
rem ninja -v -j4
CD src && meson ..\build
CD ..\build && ninja -j4
REM IF !ERRORLEVEL! NEQ 0 EXIT /B 1
CD ..

ECHO Deploying iaito
MKDIR iaito
COPY build\iaito.exe iaito\iaito.exe
rem XCOPY /S /I ..\%R2DIST%\share iaito\share
rem XCOPY /S /I ..\%R2DIST%\lib iaito\lib
rem DEL iaito\lib\*.lib
rem COPY ..\%R2DIST%\bin\*.dll iaito\
windeployqt iaito\iaito.exe
FOR %%i in (..\src\translations\*.qm) DO MOVE "%%~fi" iaito\translations

ENDLOCAL
