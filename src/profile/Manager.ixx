module;
#include <filesystem>
#include <format>
#include <string>

#include "constants.h"

export module profile.Manager;

import components.I18n;
import common.Utils;
import profile.Downloader;

using namespace std;
namespace fs = std::filesystem;

// 切换订阅: 把 data/profiles/ 下的订阅文件复制覆盖到内核配置路径
export void switch_profile(string_view path, string_view kernel_config_path) {
    if(path.empty())
        throw runtime_error(tr("dialog.profile_path_not_in_list"));

    try {
        const fs::path src_path = exe_relative_path(format("{}{}", PROFILES_DIR, path));
        const fs::path dst_path = exe_relative_path(kernel_config_path);

        // 复制文件
        copy_file(src_path, dst_path, fs::copy_options::overwrite_existing);
    } catch(const fs::filesystem_error& e) {
        // 抛出异常给调用函数, 错误文件绝对路径
        const fs::path errorPath = absolute(e.path1());
        const string msg =
                format("{}\n{}", errorPath.string(), tr("dialog.check_profile_list"));
        throw runtime_error(msg);
    }
}

// 更新订阅: 从远程 URL 下载到 data/profiles/ 下
export bool update_profile(string_view url, string_view ua, string_view path) {
    if(url.empty() || path.empty()) {
        throw runtime_error(tr("dialog.profile_path_not_in_list"));
    }

    const fs::path dst_path = exe_relative_path(format("{}{}", PROFILES_DIR, path));
    return download_profile(url, ua, dst_path);
}
