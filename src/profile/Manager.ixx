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

export void switch_profile(string_view path, string_view kernel_config_path) {
    if(path.empty())
        throw runtime_error(tr("dialog.profile_path_not_in_list"));

    try {
        const fs::path src_path = exe_relative_path(format("{}{}", PROFILES_DIR, path));
        const fs::path dst_path = exe_relative_path(kernel_config_path);
        copy_file(src_path, dst_path, fs::copy_options::overwrite_existing);
    } catch(const fs::filesystem_error& e) {
        const fs::path errorPath = absolute(e.path1());
        const string msg =
                format("{}\n{}", errorPath.string(), tr("dialog.check_profile_list"));
        throw runtime_error(msg);
    }
}

export bool update_profile(string_view url, string_view ua, string_view path) {
    if(url.empty() || path.empty()) {
        throw runtime_error(tr("dialog.profile_path_not_in_list"));
    }

    const fs::path dst_path = exe_relative_path(format("{}{}", PROFILES_DIR, path));
    return download_profile(url, ua, dst_path);
}
