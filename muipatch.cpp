/*
 * muipatch.cpp
 *
 * Windows port of muipatch.sh.
 * Scans folders for .mui files up to DEPTH subdirectories deep, finds the
 * matching .exe/.dll one level above the .mui, then patches it with
 * Resource Hacker (ResourceHacker.exe).
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

// constants
static constexpr int DEPTH = 5;

// Global log file stream. Opened once in main(), then used via logOut/logErr.
static std::wofstream gLog;

// Write a line to both stdout and the log file.
static void logOut(const std::wstring& msg)
{
    std::wcout << msg;
    if (gLog.is_open()) gLog << msg;
}

// Write a line to both stderr and the log file.
static void logErr(const std::wstring& msg)
{
    std::wcerr << msg;
    if (gLog.is_open()) gLog << msg;
}

// helpers

// Returns the directory that contains the running executable.
static fs::path exeDir()
{
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path();
}

// Case-insensitive wide-string comparison.
static bool iequal(const std::wstring& a, const std::wstring& b)
{
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(),
                      [](wchar_t x, wchar_t y){ return towlower(x) == towlower(y); });
}

// Create directory tree, returns false on failure.
static bool mkdirp(const fs::path& p)
{
    std::error_code ec;
    fs::create_directories(p, ec);
    return !ec;
}

// Copy a file, overwriting the destination.  Returns false on failure.
static bool copyFile(const fs::path& src, const fs::path& dst)
{
    std::error_code ec;
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    return !ec;
}

// Run a command synchronously (no console window) and wait for it to finish.
static bool runProcess(const std::wstring& cmdline)
{
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    // Hide the Resource Hacker window
    si.dwFlags  = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};

    // CreateProcessW requires a mutable buffer
    std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
    buf.push_back(L'\0');

    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr,
        FALSE, 0, nullptr, nullptr, &si, &pi))
        return false;

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

// Resource Hacker writes its logs as UTF-16LE, convert them to UTF-8 so they are readable
static void convertLogToUtf8(const fs::path& logPath)
{
    std::ifstream in(logPath, std::ios::binary);
    if (!in) return;

    std::vector<char> raw((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
    in.close();

    if (raw.size() < 2) return;

    // Detect UTF-16LE BOM (FF FE) or just assume every other byte is 0x00.
    bool hasLEbom = (static_cast<unsigned char>(raw[0]) == 0xFF &&
    static_cast<unsigned char>(raw[1]) == 0xFE);

    // if every second byte is 0x00 it is almost certainly
    // UTF-16LE (ASCII range only, which is true for RH logs).
    bool looksUtf16 = false;
    if (raw.size() >= 4)
    {
        size_t nullBytes = 0;
        for (size_t i = 1; i < std::min(raw.size(), (size_t)200); i += 2)
            if (raw[i] == 0) ++nullBytes;
            looksUtf16 = (nullBytes > 20);
    }

    if (!hasLEbom && !looksUtf16) return;

    // Build a wstring from the raw bytes, then convert via WideCharToMultiByte.
    const wchar_t* wptr = reinterpret_cast<const wchar_t*>(
        raw.data() + (hasLEbom ? 2 : 0));
    int wlen = static_cast<int>((raw.size() - (hasLEbom ? 2 : 0)) / 2);

    int needed = WideCharToMultiByte(CP_UTF8, 0, wptr, wlen,
                                     nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return;

    std::string utf8(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wptr, wlen,
                        utf8.data(), needed, nullptr, nullptr);

    std::ofstream out(logPath, std::ios::binary | std::ios::trunc);
    if (out) out.write(utf8.data(), utf8.size());
}

// Quote a path for use in a command-line argument (handles spaces).
static std::wstring quoted(const fs::path& p)
{
    return L"\"" + p.wstring() + L"\"";
}

//main loop

int wmain(int argc, wchar_t** argv)
{
    // Collect folders from argv; default to exe directory.
    std::vector<fs::path> folders;
    if (argc <= 1)
    {
        folders.push_back(exeDir());
    }
    else
    {
        for (int i = 1; i < argc; ++i)
            folders.emplace_back(argv[i]);
    }


    // Locate Resource Hacker next to this executable.
    fs::path reHacker = exeDir() / L"ResourceHacker.exe";
    if (!fs::is_regular_file(reHacker))
    {
        logErr(L"ERROR: ResourceHacker.exe not found next to this executable.\n");
        return 1;
    }
    logOut(L"Using Resource Hacker: " + reHacker.wstring() + L"\n");

    // Logs folder next to the exe, with extract/ and patch/ subfolders.
    fs::path logsDir        = exeDir() / L"logs";
    fs::path logsExtractDir = logsDir  / L"extract";
    fs::path logsPatchDir   = logsDir  / L"patch";
    if (!mkdirp(logsExtractDir) || !mkdirp(logsPatchDir))
    {
        logErr(L"ERROR: Cannot create logs directory: " + logsDir.wstring() + L"\n");
        return 1;
    }

    // Open the main log file inside the logs folder.
    fs::path logFile = logsDir / L"muimerge.log";
    gLog.open(logFile, std::ios::app);
    if (!gLog.is_open())
        std::wcerr << L"WARNING: Could not open log file: " << logFile.wstring() << L"\n";
    else
        gLog << L"\n muipatch run \n";

    int patched = 0;
    int skipped = 0;

    for (const fs::path& folder : folders)
    {
        if (!fs::is_directory(folder))
        {
            logErr(L"WARNING: '" + folder.wstring() + L"' is not a directory, skipping.\n");
            ++skipped;
            continue;
        }

        logOut(L"Scanning: " + folder.wstring() + L"\n");

        // Walk the tree up to DEPTH levels, collecting .mui files.
        // std::filesystem::recursive_directory_iterator doesn't expose depth
        // natively before C++23, so we limit it manually.
        std::vector<fs::path> muiFiles;

        // BFS / iterative DFS with depth tracking.
        struct Entry { fs::path path; int depth; };
        std::vector<Entry> stack;
        stack.push_back({folder, 0});

        while (!stack.empty())
        {
            auto [dir, depth] = stack.back();
            stack.pop_back();

            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(dir, ec))
            {
                if (ec) break;
                if (entry.is_directory() && depth < DEPTH)
                    stack.push_back({entry.path(), depth + 1});
                else if (entry.is_regular_file())
                {
                    std::wstring ext = entry.path().extension().wstring();
                    std::transform(ext.begin(), ext.end(), ext.begin(), towlower);
                    if (ext == L".mui")
                        muiFiles.push_back(entry.path());
                }
            }
        }

        for (const fs::path& muiFile : muiFiles)
        {
            // mui layout:  <gameDir>/<locale>/<binary>.mui
            //              e.g. Foo/en-US/game.exe.mui
            fs::path localeDir = muiFile.parent_path();
            fs::path gameDir   = localeDir.parent_path();
            std::wstring parentDirName = localeDir.filename().wstring();

            // Strip the trailing .mui to recover the real filename (e.g. game.exe).
            fs::path muiBasename = muiFile.filename();
            std::wstring baseName = muiBasename.stem().wstring();
            fs::path baseNamePath(baseName);
            std::wstring exeStem = baseNamePath.stem().wstring();

            // Find the matching .exe/.dll in gameDir (case-insensitive).
            fs::path target;
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(gameDir, ec))
            {
                if (!entry.is_regular_file()) continue;
                if (iequal(entry.path().filename().wstring(), baseName))
                {
                    target = entry.path();
                    break;
                }
            }

            if (target.empty())
            {
                logErr(L"  WARNING: No matching " + baseName + L" found in " + gameDir.wstring() + L", skipping.\n");
                ++skipped;
                continue;
            }

            logOut(L"  [" + parentDirName + L"] Patching " + baseName + L"...\n");

            // ── Step 1: extract resources from .mui → .res
            fs::path resFile    = fs::path(target.wstring() + L".res");
            fs::path logExtract = logsExtractDir / (exeStem + L"-extract.log");
            fs::path logPatch   = logsPatchDir   / (exeStem + L"-patch.log");

            logOut(L"  .mui to .res conversion...\n");
            {
                std::wstring cmd =
                quoted(reHacker)
                + L" -open "   + quoted(muiFile)
                + L" -save "   + quoted(resFile)
                + L" -action extract"
                + L" -mask \"*,*\""
                + L" -log "    + quoted(logExtract);

                if (!runProcess(cmd))
                {
                    logErr(L"  ERROR: Resource Hacker (extract) failed for " + muiFile.wstring() + L"\n");
                    ++skipped;
                    continue;
                }
            }

            // ── Step 2: back up the target binary
            std::wstring targetExt = target.extension().wstring();
            fs::path targetOrig    = target.parent_path()
            / (target.stem().wstring() + L"_original" + targetExt);
            if (!copyFile(target, targetOrig))
                logErr(L"  WARNING: Could not create backup " + targetOrig.wstring() + L"\n");

            // ── Step 3: patch the .res into the target binary
            logOut(L"  Patching .res into " + baseName + L"...\n");
            {
                std::wstring cmd =
                quoted(reHacker)
                + L" -open "   + quoted(target)
                + L" -save "   + quoted(target)
                + L" -action addoverwrite"
                + L" -res "    + quoted(resFile)
                + L" -mask \"*,*\""
                + L" -log "    + quoted(logPatch);

                if (!runProcess(cmd))
                {
                    logErr(L"  ERROR: Resource Hacker (patch) failed for " + target.wstring() + L"\n");
                    ++skipped;
                    continue;
                }
            }

            // ── Step 4: convert RH logs from UTF-16LE to UTF-8 ──
            convertLogToUtf8(logExtract);
            convertLogToUtf8(logPatch);

            logOut(L"  Done: " + baseName + L"\n");
            ++patched;
        }
    }

    logOut(L"\nFinished: " + std::to_wstring(patched) + L" file(s) patched, " + std::to_wstring(skipped) + L" skipped.\n");
    return 0;
}
