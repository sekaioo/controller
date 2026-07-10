module;
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>

#include "constants.h"

export module components.Logger;

using namespace std;
namespace fs = std::filesystem;

// @formatter:off
export namespace logger {
    enum Level {
        ALL = 0,
        INFO = 1,
        WARN = 2,
        ERROR = 3,
        FATAL = 4,
        OFF = 5
    };

    void set_level(Level level) noexcept;
    void info(string_view message) noexcept;
    void warn(string_view message) noexcept;
    void error(string_view message) noexcept;
    void fatal(string_view message) noexcept;
}
// @formatter:on

constexpr array LEVEL_NAMES = {"ALL", "INFO", "WARN", "ERROR", "FATAL", "OFF"};

static atomic<int> current_level_ = logger::ALL;
static mutex log_mutex_;

// 超过阈值时轮转: 旧日志顶替 .old, 全程使用不抛异常的重载
static void rotate_if_needed() {
    error_code ec;
    if(fs::exists(LOG_FILE, ec) && fs::file_size(LOG_FILE, ec) > LOG_MAX_SIZE) {
        fs::remove(LOG_FILE_OLD, ec);
        fs::rename(LOG_FILE, LOG_FILE_OLD, ec);
    }
}

// 写一行日志, 日志绝不能反过来影响程序运行, 任何异常都吞掉
static void write_line(const logger::Level level, string_view message) noexcept {
    if(level < current_level_.load(memory_order_relaxed)) return;

    try {
        lock_guard lock(log_mutex_);
        rotate_if_needed();

        ofstream file(LOG_FILE, ios::app);
        if(!file.is_open()) return;

        const auto now = chrono::floor<chrono::seconds>(chrono::system_clock::now());
        file << format("[{:%Y-%m-%d %H:%M:%S}] [{}] {}\n",
                       chrono::zoned_time{chrono::current_zone(), now},
                       LEVEL_NAMES[level], message);
    } catch(...) {
    }
}

namespace logger {
    void set_level(const Level level) noexcept {
        current_level_.store(level, memory_order_relaxed);
    }

    void info(string_view message) noexcept { write_line(INFO, message); }
    void warn(string_view message) noexcept { write_line(WARN, message); }
    void error(string_view message) noexcept { write_line(ERROR, message); }
    void fatal(string_view message) noexcept { write_line(FATAL, message); }
}
