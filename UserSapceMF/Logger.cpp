#include "pch.h"
#include "Logger.h"

#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>

using namespace std;

std::mutex log_mutex;

void DebugLog(const char* msg) {
    std::lock_guard<std::mutex> guard(log_mutex);
    std::ofstream logfile("debug.log", std::ios::app);
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
    }
}