/*
 * muipatch.cpp — patches .exe/.dll files using .mui resource files via Resource Hacker.
 *
 * Scans folders up to DEPTH levels deep for .mui files, finds the matching .exe/.dll
 * one level above, and patches it with ResourceHacker.exe (must be next to this executable).
 *
 * Build:
 *   x86_64-w64-mingw32-g++ -std=c++17 -O2 -mconsole -municode -o muipatch.exe muipatch.cpp -static-libgcc -static-libstdc++
 *
 * Usage:
 *   muipatch.exe [folder1] [folder2] ...
 *   If no folders are given, the directory containing the exe is used.
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace std;

static constexpr int DEPTH = 5;

// Log file, written alongside console output
static wofstream gLog;

static void log(const wstring& msg, wostream& out = wcout) {
    out << msg;
    if (gLog.is_open()) gLog << msg;
}

// Utilities
// Directory of the running exe
static fs::path exeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path();
}

// Case-insensitive wide string comparison
static bool iequal(const wstring& a, const wstring& b) {
    if (a.size() != b.size()) return false;
    return equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y){ return towlower(x) == towlower(y); });
}

// Recursive mkdir
static bool mkdirp(const fs::path& p) {
    error_code ec;
    fs::create_directories(p, ec);
    return !ec;
}

// Run a process silently and wait for it to finish
static bool runProcess(const wstring& cmdline) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    vector<wchar_t> buf(cmdline.begin(), cmdline.end());
    buf.push_back(L'\0');

    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

static wstring quoted(const fs::path& p) { return L"\"" + p.wstring() + L"\""; }

// Entry point

int wmain(int argc, wchar_t** argv) {
    // Collect target folders from arguments, default to exe directory
    vector<fs::path> folders;
    if (argc <= 1)
        folders.push_back(exeDir());
    else
        for (int i = 1; i < argc; ++i)
            folders.emplace_back(argv[i]);

    // Locate Resource Hacker
    fs::path reHacker = exeDir() / L"ResourceHacker.exe";
    if (!fs::is_regular_file(reHacker)) {
        log(L"ERROR: ResourceHacker.exe not found next to this executable.\n", wcerr);
        return 1;
    }
    log(L"Using Resource Hacker: " + reHacker.wstring() + L"\n");

    // Set up logs folder and main log file
    fs::path logsDir = exeDir() / L"logs";
    if (!mkdirp(logsDir)) {
        log(L"ERROR: Cannot create logs directory: " + logsDir.wstring() + L"\n", wcerr);
        return 1;
    }
    gLog.open(logsDir / L"muimerge.log", ios::app);
    if (!gLog.is_open()) wcerr << L"WARNING: Could not open log file.\n";
    else gLog << L"\n=== muipatch run ===\n";

    int patched = 0, skipped = 0;

    // Main patching loop — one folder at a time
    for (const fs::path& folder : folders) {
        if (!fs::is_directory(folder)) {
            log(L"WARNING: '" + folder.wstring() + L"' is not a directory, skipping.\n", wcerr);
            ++skipped;
            continue;
        }

        log(L"Scanning: " + folder.wstring() + L"\n");

        // Collect all .mui files up to DEPTH levels deep
        vector<fs::path> muiFiles;
        struct Entry { fs::path path; int depth; };
        vector<Entry> stack;
        stack.push_back({folder, 0});

        while (!stack.empty()) {
            auto [dir, depth] = stack.back();
            stack.pop_back();
            error_code ec;
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (ec) break;
                if (entry.is_directory() && depth < DEPTH) stack.push_back({entry.path(), depth + 1});
                else if (entry.is_regular_file()) {
                    wstring ext = entry.path().extension().wstring();
                    transform(ext.begin(), ext.end(), ext.begin(), towlower);
                    if (ext == L".mui") muiFiles.push_back(entry.path());
                }
            }
        }

        // Patch each .mui into its matching exe/dll
        for (const fs::path& muiFile : muiFiles) {
            // .mui sits in <gameDir>/<locale>/<name>.exe.mui
            fs::path localeDir     = muiFile.parent_path();
            fs::path gameDir       = localeDir.parent_path();
            wstring parentDir = localeDir.filename().wstring();
            wstring baseName  = muiFile.filename().stem().wstring(); // e.g. game.exe
            wstring exeStem   = fs::path(baseName).stem().wstring(); // e.g. game

            // Find matching target in parent dir, case-insensitive
            fs::path target;
            error_code ec;
            for (const auto& entry : fs::directory_iterator(gameDir, ec)) {
                if (!entry.is_regular_file()) continue;
                if (iequal(entry.path().filename().wstring(), baseName)) { target = entry.path(); break; }
            }

            if (target.empty()) {
                log(L"  WARNING: No matching " + baseName + L" found in " + gameDir.wstring() + L", skipping.\n", wcerr);
                ++skipped;
                continue;
            }

            log(L"  [" + parentDir + L"] Patching " + baseName + L"...\n");

            fs::path resFile    = fs::path(target.wstring() + L".res");
            fs::path logExtract = logsDir / (exeStem + L"-extract.log");
            fs::path logPatch   = logsDir / (exeStem + L"-patch.log");

            // Step 1: extract resources from .mui into a .res file
            log(L"  .mui to .res conversion...\n");
            if (!runProcess(quoted(reHacker) + L" -open " + quoted(muiFile) + L" -save " + quoted(resFile) + L" -action extract -mask \"*,*\" -log " + quoted(logExtract))) {
                log(L"  ERROR: Resource Hacker (extract) failed for " + muiFile.wstring() + L"\n", wcerr);
                ++skipped;
                continue;
            }

            // Step 2: merge .res into the target binary
            log(L"  Patching .res into " + baseName + L"...\n");
            if (!runProcess(quoted(reHacker) + L" -open " + quoted(target) + L" -save " + quoted(target) + L" -action addoverwrite -res " + quoted(resFile) + L" -mask \"*,*\" -log " + quoted(logPatch))) {
                log(L"  ERROR: Resource Hacker (patch) failed for " + target.wstring() + L"\n", wcerr);
                ++skipped;
                continue;
            }

            log(L"  Done: " + baseName + L"\n");
            ++patched;
        }
    }

    log(L"\nFinished: " + to_wstring(patched) + L" file(s) patched, " + to_wstring(skipped) + L" skipped.\n");
    return 0;
}
