# .mui-patcher
Simple script that uses Resource Hacker to find all .mui files in a folder and patch them to their respective executables

## Prerequisites
Shell file: Wine, [ResourceHacker.exe(direct download)](https://www.angusj.com/resourcehacker/resource_hacker.zip) in same directory as script
Executable: Wine or Windows, same ResourceHacker.exe position.

## Operation
Either execute the script in the folder wanted, or pass it as a variable. It creates a `logs` directory in which logs for every patching sequence is stored.

By default it will search 5 subfolders deep for the .mui file (and thus 4 layers for the .exe/.dll file), then match them case insensitively (chess.exe -> Chess.exe.mui)

The script intentionally leaves all files created behind (.res intermediate step, _original.exe/.dll as a backup). To change any unwanted behaviour modify the script itself.

## About project
This uses a better method of patching .mui files (taken from [this older project](https://github.com/Juergen74/install-windows7games) that I did not find before) that completely removes the need for python or patching scripts. The main script is not using this route yet as the installer from Aero ([the website](https://win7games.com/#games)) keeps getting modified, and I left it for later.
