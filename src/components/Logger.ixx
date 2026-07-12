module;
#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>

export module components.Logger;

import common.Utils;

using namespace std;
namespace fs = std::filesystem;

// @formatter:off
export class Logger {
public:
    enum Level {
        TRACE = 0,
        INFO  = 1,
        WARN  = 2,
        ERROR = 3,
        FATAL = 4
    };
    static void initialize(bool disabled, string_view level, string_view output, bool timestamp) noexcept;
    static void log(string_view message, Level level) noexcept;
private:
    inline static mutex log_mutex_;
    inline static ofstream output_stream_;
    inline static bool disabled_;
    inline static Level level_;
    inline static bool timestamp_;
};
// @formatter:on

static Logger::Level parse_log_level(string_view name) {
    if(name == "trace") return Logger::TRACE;
    if(name == "info") return Logger::INFO;
    if(name == "warn") return Logger::WARN;
    if(name == "error") return Logger::ERROR;
    if(name == "fatal") return Logger::FATAL;
    throw runtime_error(format("Field 'log_level' has invalid value: {}", name));
}

void Logger::initialize(bool disabled, string_view level, string_view output, bool timestamp) noexcept {
    disabled_ = disabled;
    level_ = parse_log_level(level);
    timestamp_ = timestamp;

    const fs::path log_file = exe_relative_path(output);
    output_stream_ = ofstream(log_file, ofstream::out | ofstream::trunc);
}

void Logger::log(string_view message, const Level level) noexcept {
    if(disabled_ || level < level_) return;

    // log format with timestamp "[timestamp] [level] info"
    // log format "[level] info"
    constexpr static array level_names = {"ALL", "INFO", "WARN", "ERROR", "FATAL"};
    constexpr static string_view log_format_with_timestamp = "[{}] [{}] {}\n";
    constexpr static string_view log_format = "[{}] {}\n";
    try {
        lock_guard lock(log_mutex_);
        string log_level = level_names[level];
        if(timestamp_) {
            const auto now = chrono::floor<chrono::seconds>(chrono::system_clock::now());
            string timestamp = format("{:%Y-%m-%d %H:%M:%S}", chrono::zoned_time{
                                          chrono::current_zone(), now
                                      });
            output_stream_ << format(log_format_with_timestamp, timestamp, log_level, message) << std::flush;;
        } else {
            output_stream_ << format(log_format, log_level, message) << std::flush;;
        }
    } catch(...) {}
}
