# .mui Patcher
Simple script that uses Resource Hacker to find all .mui files in a folder and patch them to their respective executables

## Prerequisites
ResourceHacker.exe ([direct .zip download to extract from](https://www.angusj.com/resourcehacker/resource_hacker.zip)) in same directory as script

### Versions

Bash script: for Linux, Wine accessible in PATH

Powershell script: for Wine or Windows, PowerShell ([zip download](https://learn.microsoft.com/en-us/powershell/scripting/install/install-powershell-on-windows?view=powershell-7.6#zip)) if not already installed

## What it does

Either execute the script in the folder wanted, or pass it as a variable. It creates a `logs` directory in which logs for every patching sequence is stored.

By default it will search 5 subfolders deep for the .mui file (and thus 4 layers for the .exe/.dll file), then match them case insensitively (chess.exe -> Chess.exe.mui)

The script intentionally leaves .res files it makes for simplicity, but original executables are modified without backups left behind, unlike non-CLI behaviour of the app used

The bash script will use the ~/.wine directory by default but can be changed by passing env WINEPREFIX= to it as with wine itself.

## About project
This uses a better method of patching .mui files (taken from [this older project](https://github.com/Juergen74/install-windows7games) that I did not find before) that completely removes the need for python or patching scripts. The main script is not using this route yet as the installer from Aero ([the website](https://win7games.com/#games)) keeps getting modified, and I left it for later.
