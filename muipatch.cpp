/*
 * muipatch.cpp
 *
 * Windows port of muipatch.sh.
 * Scans folders for .mui files up to DEPTH subdirectories deep, finds the matching .exe/.dll
 * one level above the .mui, then patches it with Resource Hacker (ResourceHacker.exe).
 *
 * Build (MinGW-w64 / cross-compile on Linux):
 *   x86_64-w64-mingw32-g++ -std=c++17 -O2 -mconsole -o muipatch.exe muipatch.cpp -static-libgcc -static-libstdc++
 *
 * Build (MSVC, untested):
 *   cl /EHsc /std:c++17 muipatch.cpp
 *
 * Usage (inside Wine or native Windows):
 *   muipatch.exe [folder1] [folder2] ...
 *   If no folders are given the directory containing the .exe is used.
 *
 * ResourceHacker.exe must be placed next to this executable.
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

static constexpr int DEPTH = 5;

static std::wofstream gLog;

static void logOut(const std::wstring& msg) {
    std::wcout << msg;
    if (gLog.is_open()) gLog << msg;
}

static void logErr(const std::wstring& msg) {
    std::wcerr << msg;
    if (gLog.is_open()) gLog << msg;
}

static fs::path exeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path();
}

static bool iequal(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y){ return towlower(x) == towlower(y); });
}

static bool mkdirp(const fs::path& p) {
    std::error_code ec;
    fs::create_directories(p, ec);
    return !ec;
}

static bool runProcess(const std::wstring& cmdline) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
    buf.push_back(L'\0');

    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

static std::wstring quoted(const fs::path& p) { return L"\"" + p.wstring() + L"\""; }

int wmain(int argc, wchar_t** argv) {
    std::vector<fs::path> folders;
    if (argc <= 1)
        folders.push_back(exeDir());
    else
        for (int i = 1; i < argc; ++i)
            folders.emplace_back(argv[i]);

    fs::path reHacker = exeDir() / L"ResourceHacker.exe";
    if (!fs::is_regular_file(reHacker)) {
        logErr(L"ERROR: ResourceHacker.exe not found next to this executable.\n");
        return 1;
    }
    logOut(L"Using Resource Hacker: " + reHacker.wstring() + L"\n");

    fs::path logsDir = exeDir() / L"logs";
    if (!mkdirp(logsDir)) {
        logErr(L"ERROR: Cannot create logs directory: " + logsDir.wstring() + L"\n");
        return 1;
    }

    gLog.open(logsDir / L"muimerge.log", std::ios::app);
    if (!gLog.is_open()) std::wcerr << L"WARNING: Could not open log file.\n";
    else gLog << L"\n=== muipatch run ===\n";

    int patched = 0, skipped = 0;

    for (const fs::path& folder : folders) {
        if (!fs::is_directory(folder)) {
            logErr(L"WARNING: '" + folder.wstring() + L"' is not a directory, skipping.\n");
            ++skipped;
            continue;
        }

        logOut(L"Scanning: " + folder.wstring() + L"\n");

        std::vector<fs::path> muiFiles;
        struct Entry { fs::path path; int depth; };
        std::vector<Entry> stack;
        stack.push_back({folder, 0});

        while (!stack.empty()) {
            auto [dir, depth] = stack.back();
            stack.pop_back();
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (ec) break;
                if (entry.is_directory() && depth < DEPTH) stack.push_back({entry.path(), depth + 1});
                else if (entry.is_regular_file()) {
                    std::wstring ext = entry.path().extension().wstring();
                    std::transform(ext.begin(), ext.end(), ext.begin(), towlower);
                    if (ext == L".mui") muiFiles.push_back(entry.path());
                }
            }
        }

        for (const fs::path& muiFile : muiFiles) {
            fs::path localeDir     = muiFile.parent_path();
            fs::path gameDir       = localeDir.parent_path();
            std::wstring parentDir = localeDir.filename().wstring();
            std::wstring baseName  = muiFile.filename().stem().wstring();
            std::wstring exeStem   = fs::path(baseName).stem().wstring();

            fs::path target;
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(gameDir, ec)) {
                if (!entry.is_regular_file()) continue;
                if (iequal(entry.path().filename().wstring(), baseName)) { target = entry.path(); break; }
            }

            if (target.empty()) {
                logErr(L"  WARNING: No matching " + baseName + L" found in " + gameDir.wstring() + L", skipping.\n");
                ++skipped;
                continue;
            }

            logOut(L"  [" + parentDir + L"] Patching " + baseName + L"...\n");

            fs::path resFile    = fs::path(target.wstring() + L".res");
            fs::path logExtract = logsDir / (exeStem + L"-extract.log");
            fs::path logPatch   = logsDir / (exeStem + L"-patch.log");

            logOut(L"  .mui to .res conversion...\n");
            if (!runProcess(quoted(reHacker) + L" -open " + quoted(muiFile) + L" -save " + quoted(resFile) + L" -action extract -mask \"*,*\" -log " + quoted(logExtract))) {
                logErr(L"  ERROR: Resource Hacker (extract) failed for " + muiFile.wstring() + L"\n");
                ++skipped;
                continue;
            }

            logOut(L"  Patching .res into " + baseName + L"...\n");
            if (!runProcess(quoted(reHacker) + L" -open " + quoted(target) + L" -save " + quoted(target) + L" -action addoverwrite -res " + quoted(resFile) + L" -mask \"*,*\" -log " + quoted(logPatch))) {
                logErr(L"  ERROR: Resource Hacker (patch) failed for " + target.wstring() + L"\n");
                ++skipped;
                continue;
            }

            logOut(L"  Done: " + baseName + L"\n");
            ++patched;
        }
    }

    logOut(L"\nFinished: " + std::to_wstring(patched) + L" file(s) patched, " + std::to_wstring(skipped) + L" skipped.\n");
    return 0;
}
