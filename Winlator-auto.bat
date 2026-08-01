@echo off
setlocal

set "GAMES_DIR=C:\Program Files\Microsoft Games"

"%~dp0Windows 7 Games for Windows 11, 10 and 8-4.2-setup.exe" /SP- /SILENT
"%~dp0muipatch.exe" "%GAMES_DIR%"

for /r "%GAMES_DIR%" %%F in (*.png *.res) do del /f /q "%%F"

xcopy /E /I /Y "%GAMES_DIR%" "D:\Microsoft Games\"

endlocal
