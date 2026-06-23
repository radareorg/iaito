@echo off
rem Register the iaito:// URI scheme handler for the current user.
rem Usage: register.bat [path-to-iaito.exe]

setlocal
set "IAITO=%~1"
if "%IAITO%"=="" set "IAITO=%~dp0..\..\build\iaito.exe"

reg add "HKCU\Software\Classes\iaito" /ve /d "URL:iaito Protocol" /f
reg add "HKCU\Software\Classes\iaito" /v "URL Protocol" /d "" /f
reg add "HKCU\Software\Classes\iaito\DefaultIcon" /ve /d "\"%IAITO%\",0" /f
reg add "HKCU\Software\Classes\iaito\shell\open\command" /ve /d "\"%IAITO%\" \"%%1\"" /f
echo Registered iaito:// -^> %IAITO%
endlocal
