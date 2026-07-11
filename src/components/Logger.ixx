module;
#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>

#include "constants.h"

export module components.Logger;

import common.Utils;

using namespace std;
namespace fs = std::filesystem;

// @formatter:off
export class Logger {
public:
    enum Level {
        ALL = 0,
        INFO = 1,
        WARN = 2,
        ERROR = 3,
        FATAL = 4,
        OFF = 5
    };
    inline static Level level = ALL;
    static void log_with_date_time(string_view message, Level level = ALL) noexcept;
private:
    static void rotate_if_needed();
    inline static mutex log_mutex_;
};
// @formatter:on

void Logger::rotate_if_needed() {
    const fs::path log_file = exe_relative_path(LOG_FILE);
    error_code ec;
    if(fs::exists(log_file, ec) && fs::file_size(log_file, ec) > LOG_MAX_SIZE) {
        const fs::path log_file_old = exe_relative_path(LOG_FILE_OLD);
        fs::remove(log_file_old, ec);
        fs::rename(log_file, log_file_old, ec);
    }
}

void Logger::log_with_date_time(string_view message, const Level message_level) noexcept {
    if(message_level < level) return;

    try {
        constexpr array level_names = {"ALL", "INFO", "WARN", "ERROR", "FATAL", "OFF"};
        lock_guard lock(log_mutex_);
        rotate_if_needed();

        ofstream file(exe_relative_path(LOG_FILE), ios::app);
        if(!file.is_open()) return;

        const auto now = chrono::floor<chrono::seconds>(chrono::system_clock::now());
        file << format("[{:%Y-%m-%d %H:%M:%S}] [{}] {}\n",
                       chrono::zoned_time{chrono::current_zone(), now},
                       level_names[message_level], message);
    } catch(...) {
    }
}
