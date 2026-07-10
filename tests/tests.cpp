// controller 单元测试: 无框架, CHECK 计数失败, 退出码非 0 表示有用例失败
#include <Windows.h>
#include <shellapi.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

// windows.h 定义的 ERROR 宏与 Log::ERROR 冲突
#ifdef ERROR
#undef ERROR
#endif

#pragma comment(lib, "shell32.lib")

import common.Utils;
import components.Config;
import components.Log;

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
  "kernel": {"path": "k", "command": "c", "config_path": "cc"},
  "profiles": {"a": {"path": "a.json", "url": "https://example.com"}}
})";

static void test_config_valid() {
    const Config config = load_config(VALID_CONFIG);
    CHECK(config.lang == "en-US");
    CHECK(config.ua == "curl");
    CHECK(config.block_network == true);
    CHECK(config.kernel.path == "k");
    CHECK(config.kernel.command == "c");
    CHECK(config.kernel.config_path == "cc");
    CHECK(config.profiles.size() == 1);
    CHECK(config.profiles.at("a").path == "a.json");
    CHECK(config.profiles.at("a").url == "https://example.com");
    // log_level 缺省时为 ALL
    CHECK(config.log_level == Log::ALL);
}

static void test_config_log_level() {
    const auto with_level = [](string_view level) {
        return load_config(string(R"({
          "lang": "l", "ua": "u", "block_network": false, "log_level": ")")
                           + string(level) + R"(",
          "kernel": {"path": "k", "command": "c", "config_path": "cc"},
          "profiles": {}
        })");
    };
    CHECK(with_level("all").log_level == Log::ALL);
    CHECK(with_level("info").log_level == Log::INFO);
    CHECK(with_level("warn").log_level == Log::WARN);
    CHECK(with_level("error").log_level == Log::ERROR);
    CHECK(with_level("fatal").log_level == Log::FATAL);
    CHECK(with_level("off").log_level == Log::OFF);
    // 只接受小写等级名
    CHECK(throws([&] { with_level("INFO"); }));
    CHECK(throws([&] { with_level("verbose"); }));
}

static void test_config_invalid() {
    // 缺失字段
    CHECK(throws([] {
        load_config(R"({"lang": "l", "block_network": true,
            "kernel": {"path": "k", "command": "c", "config_path": "cc"},
            "profiles": {}})");
    }));
    // 字段类型错误
    CHECK(throws([] {
        load_config(R"({"lang": "l", "ua": "u", "block_network": "yes",
            "kernel": {"path": "k", "command": "c", "config_path": "cc"},
            "profiles": {}})");
    }));
    // kernel 子字段缺失
    CHECK(throws([] {
        load_config(R"({"lang": "l", "ua": "u", "block_network": true,
            "kernel": {"path": "k", "command": "c"},
            "profiles": {}})");
    }));
    // profiles 子字段缺失
    CHECK(throws([] {
        load_config(R"({"lang": "l", "ua": "u", "block_network": true,
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

int main() {
    test_utf8_to_wide();
    test_quote_argument();
    test_config_valid();
    test_config_log_level();
    test_config_invalid();

    if(failures == 0)
        printf("ALL PASS\n");
    else
        printf("%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
