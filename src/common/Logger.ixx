module;
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>

#include "constants.h"

export module common.Logger;

using namespace std;
namespace fs = std::filesystem;

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
static void write_line(string_view level, string_view message) noexcept {
    try {
        lock_guard lock(log_mutex_);
        rotate_if_needed();

        ofstream file(LOG_FILE, ios::app);
        if(!file.is_open()) return;

        const auto now = chrono::floor<chrono::seconds>(chrono::system_clock::now());
        file << format("[{:%Y-%m-%d %H:%M:%S}] [{}] {}\n",
                       chrono::zoned_time{chrono::current_zone(), now}, level, message);
    } catch(...) {
    }
}

export namespace logger {
    void info(string_view message) noexcept { write_line("INFO", message); }
    void error(string_view message) noexcept { write_line("ERROR", message); }
}
