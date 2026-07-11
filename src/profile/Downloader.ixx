module;
#include <Windows.h>

#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <system_error>

// windows.h 定义的 ERROR 宏与 Log::ERROR 冲突
#ifdef ERROR
#undef ERROR
#endif

export module profile.Downloader;

import components.Config;
import components.Log;
import common.Common;
import common.Utils;

using namespace std;
namespace fs = std::filesystem;

constexpr auto CURL_COMMAND = L"curl -fL -A {} -o {} {}";
constexpr auto TEMP_FILE_NAME = L"controller_download_{}.tmp";

// @formatter:off
static fs::path make_temp_path();
static optional<DWORD> wait_for_exit(HANDLE process, DWORD timeout_ms);
static bool is_valid_json(const fs::path& path);
//@formatter:on

export bool download_profile(string_view target_url, string_view ua, const fs::path& target_path) {
    const fs::path temp_path = make_temp_path();
    const auto temp_cleanup = [&]() noexcept {
        error_code ec;
        fs::remove(temp_path, ec);
    };
    ScopeGuard on_exit(temp_cleanup);

    // 启动 curl 子进程, 参数按 Windows 命令行规则转义, 防止引号破坏命令
    wstring command = format(CURL_COMMAND,
                             quote_argument(utf8_to_wide(ua)),
                             quote_argument(temp_path.wstring()),
                             quote_argument(utf8_to_wide(target_url))
    );
    HANDLE raw_handle = launch_hidden_process(command.c_str());
    if(!raw_handle) {
        Log::log_with_date_time(format("launch curl failed: {}", target_url), Log::ERROR);
        return false;
    }
    HandleGuard process{raw_handle};

    // 等待最多 8 秒
    const auto exit_code = wait_for_exit(process.h, 8000);
    if(!exit_code) {
        Log::log_with_date_time(format("download timed out: {}", target_url), Log::ERROR);
        return false;
    }
    if(*exit_code != 0) {
        Log::log_with_date_time(
            format("curl exited with code {}: {}", *exit_code, target_url), Log::ERROR);
        return false;
    }

    // 校验 JSON
    if(target_path.extension() == ".json" && !is_valid_json(temp_path)) {
        Log::log_with_date_time(
            format("downloaded profile is not valid JSON: {}", target_url), Log::ERROR);
        return false;
    }

    // 移动到目标位置
    try {
        fs::copy_file(temp_path, target_path, fs::copy_options::overwrite_existing);
        on_exit.dismiss();
        Log::log_with_date_time(format("profile updated: {}", target_path.string()), Log::INFO);
        return true;
    } catch(const fs::filesystem_error& e) {
        Log::log_with_date_time(format("copy profile to target failed: {}", e.what()), Log::ERROR);
        return false;
    }
}

static fs::path make_temp_path() {
    const auto ns = chrono::duration_cast<chrono::nanoseconds>(
                chrono::system_clock::now().time_since_epoch())
            .count();
    return fs::temp_directory_path() / format(TEMP_FILE_NAME, ns);
}

static optional<DWORD> wait_for_exit(HANDLE process, DWORD timeout_ms) {
    if(WaitForSingleObject(process, timeout_ms) == WAIT_TIMEOUT) {
        TerminateProcess(process, 1);
        WaitForSingleObject(process, 2000);
        return std::nullopt;
    }
    DWORD exit_code{};
    GetExitCodeProcess(process, &exit_code);
    return exit_code;
}

static bool is_valid_json(const fs::path& path) {
    ifstream file(path);
    if(!file.is_open()) return false;

    rapidjson::IStreamWrapper isw(file);
    rapidjson::Document doc;
    doc.ParseStream(isw);
    return !doc.HasParseError();
}
