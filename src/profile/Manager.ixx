module;
#include <filesystem>
#include <map>
#include <ranges>
#include <string>
#include <vector>

#include "constants.h"

export module profile.Manager;

import components.Config;
import components.I18n;
import profile.Downloader;

using namespace std;
namespace fs = std::filesystem;

// @formatter:off
export class ProfileManager {
public:
    inline static vector<string> profiles_names;
    static void initialize(map<string, Config::Profile>);
    static string get_profile_name(int index);
    static void switch_profile(string_view path, string_view kernel_config_path);
    static bool update_profile(string_view url, string_view ua, string_view path);
};
// @formatter:on

void ProfileManager::initialize(map<string, Config::Profile> profiles) {
    for (const auto& key : profiles | views::keys)
    {
        profiles_names.push_back(key);
    }
}

string ProfileManager::get_profile_name(int index) {
    return profiles_names[index];
}

void ProfileManager::switch_profile(string_view path, string_view kernel_config_path) {
    if(path.empty())
        throw runtime_error(tr("dialog.profile_path_not_in_list"));

    try {
        const fs::path src_path = format("{}{}", PROFILES_DIR, path);
        const fs::path dst_path = kernel_config_path;

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

bool ProfileManager::update_profile(string_view url, string_view ua, string_view path) {
    if(url.empty() || path.empty()) {
        throw runtime_error(tr("dialog.profile_path_not_in_list"));
    }

    const fs::path dst_path = format("{}{}", PROFILES_DIR, path);
    return download_profile(url, ua, dst_path);
}
