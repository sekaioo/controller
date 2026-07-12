module;
#include <atomic>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <ranges>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "constants.h"

export module profile.Manager;

import components.Config;
import components.I18n;
import components.Logger;
import common.Utils;
import profile.Downloader;

using namespace std;
namespace fs = std::filesystem;

// @formatter:off
export class ProfileManager {
public:
    enum class UpdateResult { Busy, Success, Failed };

    explicit ProfileManager(const Config& config) :
        profiles_(config.profiles),
        ua_(config.ua),
        kernel_config_path_(config.kernel.config_path),
        names_(build_names(profiles_)),
        current_index_(detect_current()),
        update_state_(make_shared<UpdateState>()) {}

    [[nodiscard]] const vector<string>& names() const { return names_; }
    [[nodiscard]] size_t count() const { return names_.size(); }
    [[nodiscard]] int current_index() const { return current_index_; }
    [[nodiscard]] bool is_updating() const { return update_state_->updating.load(); }
    void switch_profile(int index);
    void update_profile(string_view name, function<void(UpdateResult)> on_result) const;
    void update_all_profile(function<void(UpdateResult)> on_result) const;
    [[nodiscard]] string take_errors() const;
private:
    struct UpdateState {
        mutex mutex;
        queue<string> errors;
        atomic<bool> updating = false;
    };

    [[nodiscard]] int detect_current() const;
    static vector<string> build_names(const map<string, Config::Profile>& profiles);
    static bool download_one(const Config::Profile& profile, string_view ua);
    // 在后台线程逐个更新 tasks, 通过 on_result 回调结果; 已在更新则回调 Busy
    void run_updates(vector<pair<string, Config::Profile>> tasks, function<void(UpdateResult)> on_result) const;

    map<string, Config::Profile> profiles_;
    string ua_;
    string kernel_config_path_;
    vector<string> names_;
    int current_index_;
    shared_ptr<UpdateState> update_state_;
};
// @formatter:on

int ProfileManager::detect_current() const {
    const string kernel_config = read_file_bytes(exe_relative_path(kernel_config_path_));
    if(kernel_config.empty()) return -1;

    for(size_t i = 0; i < names_.size(); ++i) {
        const auto it = profiles_.find(names_[i]);
        if(it == profiles_.end()) continue;
        const auto profile_path = exe_relative_path(format("{}{}", PROFILES_DIR, it->second.path));
        if(read_file_bytes(profile_path) == kernel_config)
            return static_cast<int>(i);
    }
    return -1;
}

vector<string> ProfileManager::build_names(const map<string, Config::Profile>& profiles) {
    vector<string> names;
    names.reserve(profiles.size());
    for(const auto& name : profiles | views::keys)
        names.push_back(name);
    return names;
}

bool ProfileManager::download_one(const Config::Profile& profile, string_view ua) {
    if(profile.url.empty() || profile.path.empty())
        throw runtime_error(tr("dialog.profile_path_not_in_list"));

    const fs::path dst_path = exe_relative_path(format("{}{}", PROFILES_DIR, profile.path));
    return download_profile(profile.url, ua, dst_path);
}

void ProfileManager::switch_profile(const int index) {
    const auto it = profiles_.find(names_[index]);
    if(it == profiles_.end() || it->second.path.empty())
        throw runtime_error(tr("dialog.profile_path_not_in_list"));

    try {
        const fs::path src_path = exe_relative_path(format("{}{}", PROFILES_DIR, it->second.path));
        const fs::path dst_path = exe_relative_path(kernel_config_path_);
        copy_file(src_path, dst_path, fs::copy_options::overwrite_existing);
    } catch(const fs::filesystem_error& e) {
        const fs::path errorPath = absolute(e.path1());
        throw runtime_error(format("{}\n{}", errorPath.string(), tr("dialog.check_profile_list")));
    }
    current_index_ = index;
}

// 更新单个订阅: 找不到时也交给 run_updates, 由 download_one 抛出并归为 Failed
void ProfileManager::update_profile(string_view name, function<void(UpdateResult)> on_result) const {
    const auto it = profiles_.find(string(name));
    Config::Profile profile = it != profiles_.end() ? it->second : Config::Profile{};
    vector<pair<string, Config::Profile>> tasks{{string(name), std::move(profile)}};
    run_updates(std::move(tasks), std::move(on_result));
}

void ProfileManager::update_all_profile(function<void(UpdateResult)> on_result) const {
    vector<pair<string, Config::Profile>> tasks(profiles_.begin(), profiles_.end());
    run_updates(std::move(tasks), std::move(on_result));
}

void ProfileManager::run_updates(vector<pair<string, Config::Profile>> tasks,
                                 function<void(UpdateResult)> on_result) const {
    if(update_state_->updating.exchange(true)) {
        on_result(UpdateResult::Busy);
        return;
    }

    thread([tasks = std::move(tasks), ua = ua_, state = update_state_, on_result = std::move(on_result)] {
        {
            lock_guard lock(state->mutex);
            state->errors = {};
        }
        const auto report_error = [&](string msg) {
            Logger::log(format("update profile failed: {}", msg), Logger::ERROR);
            lock_guard lock(state->mutex);
            state->errors.push(std::move(msg));
        };

        bool failed = false;
        for(const auto& [name, profile] : tasks) {
            try {
                if(!download_one(profile, ua)) {
                    report_error(name);
                    failed = true;
                }
            } catch(const exception& e) {
                report_error(format("{}: {}", name, e.what()));
                failed = true;
            } catch(...) {
                report_error(name);
                failed = true;
            }
        }
        state->updating.store(false);
        on_result(failed ? UpdateResult::Failed : UpdateResult::Success);
    }).detach();
}

string ProfileManager::take_errors() const {
    lock_guard lock(update_state_->mutex);
    string result;
    while(!update_state_->errors.empty()) {
        if(!result.empty()) result += '\n';
        result += update_state_->errors.front();
        update_state_->errors.pop();
    }
    return result;
}
