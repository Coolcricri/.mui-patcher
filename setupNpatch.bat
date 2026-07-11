@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "GAMES_DIR=%ProgramFiles%\Microsoft Games"

echo Installing Resource Hacker...
start /wait "" "%SCRIPT_DIR%reshacker_setup.exe" /VERYSILENT
echo Resource Hacker installed.

echo.
echo Installing Windows 7 Games...
start /wait "" "%SCRIPT_DIR%Windows 7 Games for Windows 11, 10 and 8-4.2-setup.exe" /VERYSILENT
echo Windows 7 Games installed.

echo.
echo Running muipatch...
start /wait "" "%SCRIPT_DIR%muipatch.exe" "%GAMES_DIR%"

endlocal
