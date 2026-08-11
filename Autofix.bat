@echo off
setlocal

set "GMDR=%~dp0Microsoft Games"

"%~dp0Windows 7 Games for Windows 11, 10 and 8-4.3-setup.exe" /SP- /SILENT /LOADINF=%~dp0setup.inf

"%~dp0muipatch.exe" "%GMDR%"

for /r "%GMDR%" %%F in (*.png *.res *.ico unins000.*) do del /f /q "%%F"

endlocal
