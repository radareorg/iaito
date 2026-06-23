@echo off
rem Unregister the iaito:// URI scheme handler for the current user.
reg delete "HKCU\Software\Classes\iaito" /f
echo Unregistered iaito://
