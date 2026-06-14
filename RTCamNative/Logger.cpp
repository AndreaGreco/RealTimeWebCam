#include "pch.h"
#include "Logger.h"

#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <string>
#include <windows.h>
#include <shlobj.h>

#pragma comment(lib, "shell32.lib")

using namespace std;

std::mutex log_mutex;

// %LOCALAPPDATA%\RTVirtualCamera\debug.log, creating the folder once.
// The install dir (Program Files) is not writable for standard users.
// Falls back to the temp dir, then a relative path, if resolution fails.
// Uses SHGetFolderPathW + CSIDL (integer constant) to avoid linking the
// FOLDERID_* GUID symbols required by SHGetKnownFolderPath.
static const std::wstring& LogFilePath() {
    static const std::wstring path = []() -> std::wstring {
        std::wstring dir;
        wchar_t buf[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr,
                                       SHGFP_TYPE_CURRENT, buf))) {
            dir.assign(buf);
        }
        if (dir.empty()) {
            wchar_t tmp[MAX_PATH];
            DWORD n = GetTempPathW(MAX_PATH, tmp);
            if (n > 0 && n < MAX_PATH) dir.assign(tmp, n);
        }
        if (dir.empty()) {
            return L"debug.log"; // last resort
        }
        dir += L"\\RTVirtualCamera";
        CreateDirectoryW(dir.c_str(), nullptr); // ok if it already exists
        return dir + L"\\debug.log";
    }();
    return path;
}

void DebugLog(const char* msg) {
    std::lock_guard<std::mutex> guard(log_mutex);
    std::ofstream logfile(LogFilePath().c_str(), std::ios::app);
    if (logfile.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()) % 1000000;
        std::tm tm_buf;
        localtime_s(&tm_buf, &t);
        logfile << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
            << "." << std::setfill('0') << std::setw(6) << micros.count()
            << " - " << msg << std::endl;
		logfile.flush();
		logfile.close();
    }
}