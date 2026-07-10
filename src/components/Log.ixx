module;
#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>

#include "constants.h"

export module components.Log;

using namespace std;
namespace fs = std::filesystem;

// @formatter:off
export class Log {
public:
    enum Level {
        ALL = 0,
        INFO = 1,
        WARN = 2,
        ERROR = 3,
        FATAL = 4,
        OFF = 5
    };
    // 低于该等级的消息被丢弃, 启动时从 config.log_level 赋值
    inline static Level level = ALL;
    static void log_with_date_time(string_view message, Level level = ALL) noexcept;
private:
    static void rotate_if_needed();
    inline static mutex log_mutex_;
};
// @formatter:on

// 超过阈值时轮转: 旧日志顶替 .old, 全程使用不抛异常的重载
void Log::rotate_if_needed() {
    error_code ec;
    if(fs::exists(LOG_FILE, ec) && fs::file_size(LOG_FILE, ec) > LOG_MAX_SIZE) {
        fs::remove(LOG_FILE_OLD, ec);
        fs::rename(LOG_FILE, LOG_FILE_OLD, ec);
    }
}

// 写一行日志, 日志绝不能反过来影响程序运行, 任何异常都吞掉
void Log::log_with_date_time(string_view message, const Level message_level) noexcept {
    if(message_level < level) return;

    try {
        constexpr array level_names = {"ALL", "INFO", "WARN", "ERROR", "FATAL", "OFF"};
        lock_guard lock(log_mutex_);
        rotate_if_needed();

        ofstream file(LOG_FILE, ios::app);
        if(!file.is_open()) return;

        const auto now = chrono::floor<chrono::seconds>(chrono::system_clock::now());
        file << format("[{:%Y-%m-%d %H:%M:%S}] [{}] {}\n",
                       chrono::zoned_time{chrono::current_zone(), now},
                       level_names[message_level], message);
    } catch(...) {
    }
}
