module;
#include <filesystem>
#include <string>
#include <vector>

#include "constants.h"
export module profile.Manager;

import components.Config;
import components.I18n;
import profile.Downloader;

using namespace std;
namespace fs = std::filesystem;

export class ProfileManager {
public:
    // 获取订阅名
    static vector<string> get_profile_names();

    // 切换订阅
    static void switch_profile(string_view profile_name);

    // 更新订阅
    static bool update_profile(string_view profile_name);
};

vector<string> ProfileManager::get_profile_names() {
    return Config::instance().get_profiles_name();
}

void ProfileManager::switch_profile(string_view profile_name) {
    const string path = Config::instance().get_profile_path(profile_name);
    // 没有找到
    if(path.empty()) {
        throw runtime_error(tr("dialog.profile_path_not_in_list"));
    }

    try {
        const fs::path src_path = format("{}{}", PROFILES_DIR, path);
        const fs::path dst_path = Config::instance().get_kernel_config_path();

        // 复制文件
        copy_file(src_path, dst_path, fs::copy_options::overwrite_existing);
    } catch(const fs::filesystem_error& e) {
        // 抛出异常给调用函数, 错误文件绝对路径
        const fs::path errorPath = absolute(e.path1());
        const string msg =
                format("{}\n{}", errorPath.string(), tr("dialog.check_profile_list"));
        throw runtime_error(msg);
    }

    // 更新内存中的 webUi Url
    Config::instance().update_webUi_url();
}

bool ProfileManager::update_profile(const string_view profile_name) {
    const string url = Config::instance().get_profile_url(profile_name);
    const string path = Config::instance().get_profile_path(profile_name);

    // 没有找到
    if(url.empty() || path.empty()) {
        throw runtime_error(tr("dialog.profile_path_not_in_list"));
    }

    const fs::path dst_path = format("{}{}", PROFILES_DIR, path);
    return download_profile(url, dst_path);
}
