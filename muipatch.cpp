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
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace std;

static constexpr int DEPTH = 5;

// Output — write UTF-8 via WriteConsoleW / WriteFile, avoids wide stream locale issues

static HANDLE hOut;
static HANDLE hErr;
static HANDLE hLogFile = INVALID_HANDLE_VALUE;

static void writeHandle(HANDLE h, const wstring& msg) {
    DWORD mode;
    if (GetConsoleMode(h, &mode)) {
        DWORD n;
        WriteConsoleW(h, msg.c_str(), (DWORD)msg.size(), &n, nullptr);
    } else {
        // redirected — convert to UTF-8
        int bytes = WideCharToMultiByte(CP_UTF8, 0, msg.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (bytes > 1) {
            vector<char> buf(bytes);
            WideCharToMultiByte(CP_UTF8, 0, msg.c_str(), -1, buf.data(), bytes, nullptr, nullptr);
            DWORD n;
            WriteFile(h, buf.data(), bytes - 1, &n, nullptr);
        }
    }
}

static void writeLog(const wstring& msg) {
    if (hLogFile == INVALID_HANDLE_VALUE) return;
    int bytes = WideCharToMultiByte(CP_UTF8, 0, msg.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) return;
    vector<char> buf(bytes);
    WideCharToMultiByte(CP_UTF8, 0, msg.c_str(), -1, buf.data(), bytes, nullptr, nullptr);
    DWORD n;
    WriteFile(hLogFile, buf.data(), bytes - 1, &n, nullptr);
}

static void log(const wstring& msg, bool err = false) {
    writeHandle(err ? hErr : hOut, msg);
    writeLog(msg);
}

// Utilities

static fs::path exeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path();
}

static bool iequal(const wstring& a, const wstring& b) {
    if (a.size() != b.size()) return false;
    return equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y){ return towlower(x) == towlower(y); });
}

static bool mkdirp(const fs::path& p) {
    error_code ec;
    fs::create_directories(p, ec);
    return !ec;
}

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

static void cleanLog(const fs::path& p) {
    if (!fs::exists(p)) return;
    ifstream in(p, ios::binary);
    vector<char> data((istreambuf_iterator<char>(in)), {});
    in.close();
    vector<char> out;
    for (size_t i = 0; i < data.size(); i += 2) out.push_back(data[i]);
    ofstream o(p, ios::binary);
    o.write(out.data(), out.size());
}

// Entry point

int wmain(int argc, wchar_t** argv) {
    hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    hErr = GetStdHandle(STD_ERROR_HANDLE);

    // Collect target folders — default to directory containing this executable
    vector<fs::path> folders;
    if (argc <= 1)
        folders.push_back(exeDir());
    else
        for (int i = 1; i < argc; ++i)
            folders.emplace_back(argv[i]);

    // Locate Resource Hacker
    fs::path reHacker = exeDir() / L"ResourceHacker.exe";
    if (!fs::is_regular_file(reHacker)) {
        log(L"ERROR: ResourceHacker.exe not found next to this executable.\n", true);
        return 1;
    }
    log(L"Using Resource Hacker: " + reHacker.wstring() + L"\n");

    // Set up logs folder and main log file
    fs::path logsDir = exeDir() / L"logs";
    if (!mkdirp(logsDir)) {
        log(L"ERROR: Cannot create logs directory: " + logsDir.wstring() + L"\n", true);
        return 1;
    }

    hLogFile = CreateFileW(
        (logsDir / L"muimerge.log").c_str(),
                           GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hLogFile != INVALID_HANDLE_VALUE) {
        SetFilePointer(hLogFile, 0, nullptr, FILE_END);
        writeLog(L"\n=== muipatch run ===\n");
    } else {
        log(L"WARNING: Could not open log file.\n", true);
    }

    int patched = 0, skipped = 0;

    // Main patching loop
    for (const fs::path& folder : folders) {
        if (!fs::is_directory(folder)) {
            log(L"WARNING: '" + folder.wstring() + L"' is not a directory, skipping.\n", true);
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
            wstring baseName  = muiFile.filename().stem().wstring();
            wstring exeStem   = fs::path(baseName).stem().wstring();

            // Find matching target in parent dir, case-insensitive
            fs::path target;
            error_code ec;
            for (const auto& entry : fs::directory_iterator(gameDir, ec)) {
                if (!entry.is_regular_file()) continue;
                if (iequal(entry.path().filename().wstring(), baseName)) { target = entry.path(); break; }
            }

            if (target.empty()) {
                log(L"  WARNING: No matching " + baseName + L" found in " + gameDir.wstring() + L", skipping.\n", true);
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
                log(L"  ERROR: Resource Hacker (extract) failed for " + muiFile.wstring() + L"\n", true);
                ++skipped;
                continue;
            }
            cleanLog(logExtract);

            // Step 2: merge .res into the target binary
            log(L"  Patching .res into " + baseName + L"...\n");
            if (!runProcess(quoted(reHacker) + L" -open " + quoted(target) + L" -save " + quoted(target) + L" -action addoverwrite -res " + quoted(resFile) + L" -mask \"*,*\" -log " + quoted(logPatch))) {
                log(L"  ERROR: Resource Hacker (patch) failed for " + target.wstring() + L"\n", true);
                ++skipped;
                continue;
            }
            cleanLog(logPatch);

            log(L"  Done: " + baseName + L"\n");
            ++patched;
        }
    }

    log(L"\nFinished: " + to_wstring(patched) + L" file(s) patched, " + to_wstring(skipped) + L" skipped.\n");
    if (hLogFile != INVALID_HANDLE_VALUE) CloseHandle(hLogFile);

    for (int i = 3; i > 0; --i) {
        log(L"Closing in " + to_wstring(i) + L"...\r");
        Sleep(1000);
    }
    return 0;
}
