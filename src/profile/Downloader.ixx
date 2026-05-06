module;
#include <Windows.h>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>

export module profile.Downloader;

import components.Config;
import common.Utils;
import common.Common;

using namespace std;
namespace fs = std::filesystem;

constexpr auto CURL_COMMAND = LR"(curl -fL -A "{}" -o "{}" "{}")";
constexpr auto TEMP_FILE_NAME = L"controller_download_{}.tmp";

// @formatter:off
static fs::path make_temp_path();
static optional<DWORD> wait_for_exit(HANDLE process, DWORD timeout_ms);
static bool is_valid_json(const fs::path& path);
//@formatter:on

export bool download_profile(string_view target_url, const fs::path& target_path) {
    static const wstring ua = utf8_to_wide(Config::instance().get_ua());
    const fs::path temp_path = make_temp_path();

    // 清理临时文件
    const auto temp_cleanup = [&] { fs::remove(temp_path); };
    ScopeGuard on_exit(temp_cleanup);

    // 启动 curl 子进程
    wstring command = format(CURL_COMMAND, ua, temp_path.wstring(), utf8_to_wide(target_url.data()));
    HANDLE raw_handle = launch_hidden_process(command.c_str());
    if(!raw_handle) return false;
    HandleGuard process{raw_handle};

    // 等待最多 8 秒
    const auto exit_code = wait_for_exit(process.h, 8000);
    if(!exit_code || *exit_code != 0) return false;

    // 校验 JSON
    if(target_path.extension() == ".json" && !is_valid_json(temp_path))
        return false;

    // 移动到目标位置
    try {
        fs::copy_file(temp_path, target_path, fs::copy_options::overwrite_existing);
        on_exit.dismiss();
        return true;
    } catch(const fs::filesystem_error&) {
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
