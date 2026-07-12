// controller 单元测试: 无框架, CHECK 计数失败, 退出码非 0 表示有用例失败
#include <Windows.h>
#include <shellapi.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

// windows.h 定义的 ERROR 宏与 Logger::ERROR 冲突
#ifdef ERROR
#undef ERROR
#endif

#pragma comment(lib, "shell32.lib")

import common.Utils;
import components.Config;
import components.Logger;

using namespace std;
namespace fs = std::filesystem;

static int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if(!(cond)) {                                                     \
            ++failures;                                                   \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                 \
    } while(0)

// 期望调用抛出 std::exception
template<typename F>
static bool throws(F&& f) {
    try {
        f();
        return false;
    } catch(const exception&) {
        return true;
    }
}

// ---- utf8_to_wide ----

static void test_utf8_to_wide() {
    CHECK(utf8_to_wide("hello") == L"hello");
    CHECK(utf8_to_wide("").empty());
    // "中" 的 UTF-8 编码
    CHECK(utf8_to_wide("\xE4\xB8\xAD") == L"中");
}

// ---- quote_argument: 转义后经 CommandLineToArgvW 解析应还原出原始参数 ----

static void test_quote_argument() {
    const wstring cases[] = {
        L"simple",
        L"with space",
        L"quote\"inside",
        L"trailing\\",
        L"trailing\\\\",
        L"back\\slash\"quote",
        L"\"\"",
        L"",
        L"https://example.com/sub?token=abc&x=\"1\"",
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) \"weird\"",
        L"ends with backslash quote\\\"",
    };
    for(const auto& original : cases) {
        const wstring cmdline = L"curl.exe -o " + quote_argument(original);
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(cmdline.c_str(), &argc);
        CHECK(argv != nullptr && argc == 3 && original == argv[2]);
        if(argv) LocalFree(argv);
    }
}

// ---- Config ----

static fs::path write_temp_config(string_view content) {
    const fs::path path = fs::temp_directory_path() / "controller_test_config.json";
    ofstream file(path, ios::binary | ios::trunc);
    file << content;
    return path;
}

static Config load_config(string_view content) {
    Config config;
    config.load(write_temp_config(content));
    return config;
}

constexpr auto VALID_CONFIG = R"({
  "lang": "en-US",
  "ua": "curl",
  "block_network": true,
  "log": {"disabled": false, "level": "info", "output": "controller.log", "timestamp": true},
  "kernel": {"path": "k", "command": "c", "config_path": "cc"},
  "profiles": {"a": {"path": "a.json", "url": "https://example.com"}}
})";

static void test_config_valid() {
    const Config config = load_config(VALID_CONFIG);
    CHECK(config.lang == "en-US");
    CHECK(config.ua == "curl");
    CHECK(config.block_network == true);
    CHECK(config.log.disabled == false);
    CHECK(config.log.level == "info");
    CHECK(config.log.output == "controller.log");
    CHECK(config.log.timestamp == true);
    CHECK(config.kernel.path == "k");
    CHECK(config.kernel.command == "c");
    CHECK(config.kernel.config_path == "cc");
    CHECK(config.profiles.size() == 1);
    CHECK(config.profiles.at("a").path == "a.json");
    CHECK(config.profiles.at("a").url == "https://example.com");
}

static void test_config_log() {
    // log 各子字段解析
    const Config config = load_config(VALID_CONFIG);
    CHECK(config.log.disabled == false);
    CHECK(config.log.level == "info");
    CHECK(config.log.output == "controller.log");
    CHECK(config.log.timestamp == true);

    // log 子字段缺失 (level) 应抛出
    CHECK(throws([] {
        load_config(R"({"lang": "l", "ua": "u", "block_network": true,
            "log": {"disabled": false, "output": "o", "timestamp": true},
            "kernel": {"path": "k", "command": "c", "config_path": "cc"},
            "profiles": {}})");
    }));
    // log 子字段类型错误 (disabled) 应抛出
    CHECK(throws([] {
        load_config(R"({"lang": "l", "ua": "u", "block_network": true,
            "log": {"disabled": "no", "level": "info", "output": "o", "timestamp": true},
            "kernel": {"path": "k", "command": "c", "config_path": "cc"},
            "profiles": {}})");
    }));
}

static void test_config_invalid() {
    // 缺失字段 (ua)
    CHECK(throws([] {
        load_config(R"({"lang": "l", "block_network": true,
            "log": {"disabled": false, "level": "info", "output": "o", "timestamp": true},
            "kernel": {"path": "k", "command": "c", "config_path": "cc"},
            "profiles": {}})");
    }));
    // 字段类型错误 (block_network)
    CHECK(throws([] {
        load_config(R"({"lang": "l", "ua": "u", "block_network": "yes",
            "log": {"disabled": false, "level": "info", "output": "o", "timestamp": true},
            "kernel": {"path": "k", "command": "c", "config_path": "cc"},
            "profiles": {}})");
    }));
    // log 字段缺失
    CHECK(throws([] {
        load_config(R"({"lang": "l", "ua": "u", "block_network": true,
            "kernel": {"path": "k", "command": "c", "config_path": "cc"},
            "profiles": {}})");
    }));
    // kernel 子字段缺失 (config_path)
    CHECK(throws([] {
        load_config(R"({"lang": "l", "ua": "u", "block_network": true,
            "log": {"disabled": false, "level": "info", "output": "o", "timestamp": true},
            "kernel": {"path": "k", "command": "c"},
            "profiles": {}})");
    }));
    // profiles 子字段缺失 (url)
    CHECK(throws([] {
        load_config(R"({"lang": "l", "ua": "u", "block_network": true,
            "log": {"disabled": false, "level": "info", "output": "o", "timestamp": true},
            "kernel": {"path": "k", "command": "c", "config_path": "cc"},
            "profiles": {"a": {"path": "a.json"}}})");
    }));
    // JSON 语法错误 / 文件不存在
    CHECK(throws([] { load_config("{not json"); }));
    CHECK(throws([] {
        Config config;
        config.load("nonexistent_controller_test.json");
    }));
}

// ---- Logger: 等级阈值过滤 ----

static void test_logger() {
    const fs::path log_path = fs::temp_directory_path() / "controller_test.log";
    // 阈值 warn: INFO 应被过滤, ERROR 应写入; 不带时间戳便于断言
    Logger::initialize(false, "warn", log_path.string(), false);
    Logger::log("below-threshold", Logger::INFO);
    Logger::log("kept-message", Logger::ERROR);
    // 重新初始化到另一文件, 关闭并 flush 上一个日志文件后再读取
    Logger::initialize(true, "info", (fs::temp_directory_path() / "controller_test2.log").string(), false);

    ifstream file(log_path, ios::binary);
    const string content{istreambuf_iterator(file), istreambuf_iterator<char>()};
    CHECK(content.find("kept-message") != string::npos);
    CHECK(content.find("[ERROR]") != string::npos);
    CHECK(content.find("below-threshold") == string::npos);
}

int main() {
    test_utf8_to_wide();
    test_quote_argument();
    test_config_valid();
    test_config_log();
    test_config_invalid();
    test_logger();

    if(failures == 0)
        printf("ALL PASS\n");
    else
        printf("%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
