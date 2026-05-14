module;
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/prettywriter.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

#include "constants.h"
export module components.Config;

using namespace std;
namespace fs = std::filesystem;

export class Config {
public:
    // @formatter:off
    static Config& instance();
    void initialize() const;
    void save() const;

    // 获取顶级字段的值
    [[nodiscard]] string get_lang() const;
    [[nodiscard]] string get_ua() const;
    [[nodiscard]] bool get_block_network() const;

    // 获取 kernel 字段的值
    [[nodiscard]] string get_kernel_path() const;
    [[nodiscard]] string get_kernel_command() const;
    [[nodiscard]] string get_kernel_config_path() const;

    // 返回 profile 数组所有对象的 name
    [[nodiscard]] vector<string> get_profiles_name() const;

    // 获取 profile 字段值
    [[nodiscard]] string get_profile_path(string_view profile_name) const;
    [[nodiscard]] string get_profile_url(string_view profile_name) const;

    // 获取 webUi Url
    [[nodiscard]] string get_webUi_url();

    void update_webUi_url();

    // 禁用拷贝和移动操作
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    Config(Config&&) = delete;
    Config& operator=(Config&&) = delete;
    // @formatter:on

private:
    // 构造与析构函数
    Config()
    : json_(make_unique<rapidjson::Document>()),
      config_file_path_(CONFIG_FILE) {}

    ~Config() {
        if(dirty_) save();
    }

    // 返回 profile_name对应 profile 的对象指针, 如果没找到则返回 nullptr
    [[nodiscard]] const rapidjson::Value* find_profile(
        string_view profile_name) const;

    // json 检查函数 @formatter:off
    void validate_config_file() const;
    void validate_kernel() const;
    void validate_profiles() const;
    // @formatter:on

    unique_ptr<rapidjson::Document> json_;  // json 数据
    fs::path config_file_path_;             // config 文件路径
    bool dirty_ = false;                    // 需要写入文件标志
    string webUi_url_;                      // webUi Url
};

export class json_field_error final : public logic_error {
public:
    using Mybase = logic_error;

    explicit json_field_error(const string& Message) : Mybase(Message.c_str()) {}
    explicit json_field_error(const char* Message) : Mybase(Message) {}
};  // json 解析字段错误类

Config& Config::instance() {
    static Config instance;
    return instance;
}

void Config::initialize() const {
    // 打开配置文件
    ifstream file(config_file_path_);
    if(!file.is_open()) {
        throw runtime_error("open config file failed");
    }

    // json 处理
    rapidjson::IStreamWrapper isw(file);
    json_->ParseStream(isw);

    // 检查 json
    if(json_->HasParseError()) {
        throw runtime_error("JSON parse error");
    }

    // 验证配置文件字段
    validate_config_file();

    // 写入值给 webUi Url
    instance().update_webUi_url();
}

void Config::save() const {
    // 打开文件
    ofstream file(config_file_path_);
    if(!file.is_open()) {
        return;
    }

    // json 写入
    rapidjson::OStreamWrapper osw(file);
    rapidjson::PrettyWriter writer(osw);
    json_->Accept(writer);
}

string Config::get_lang() const { return (*json_)["lang"].GetString(); }
string Config::get_ua() const { return (*json_)["ua"].GetString(); }
bool Config::get_block_network() const { return (*json_)["block_network"].GetBool(); }
string Config::get_kernel_path() const { return (*json_)["kernel"]["path"].GetString(); }
string Config::get_kernel_command() const { return (*json_)["kernel"]["command"].GetString(); }
string Config::get_kernel_config_path() const { return (*json_)["kernel"]["config_path"].GetString(); }

vector<string> Config::get_profiles_name() const {
    vector<string> names;
    const auto& profiles = (*json_)["profiles"];
    names.reserve(profiles.Size());

    for(const auto& profile : profiles.GetArray()) {
        names.emplace_back(profile["name"].GetString());
    }
    return names;
}

string Config::get_profile_path(string_view profile_name) const {
    if(const auto* profile = find_profile(profile_name)) {
        return (*profile)["path"].GetString();
    }
    return "";
}

string Config::get_profile_url(string_view profile_name) const {
    if(const auto* profile = find_profile(profile_name)) {
        return (*profile)["url"].GetString();
    }
    return "";
}

string Config::get_webUi_url() { return webUi_url_; }

void Config::update_webUi_url() {
    // 打开内核配置文件
    const filesystem::path kernel_config_path =
            instance().get_kernel_config_path();
    ifstream file(kernel_config_path);
    if(!file.is_open()) {
        webUi_url_ = "";
        return;
    }

    // json 处理
    rapidjson::Document json;
    rapidjson::IStreamWrapper isw(file);
    json.ParseStream(isw);

    if(json.HasParseError() || !json.IsObject()) {
        webUi_url_ = "";
        return;
    }

    // 检查有无 experimental 键
    if(!json.HasMember("experimental") || !json["experimental"].IsObject()) {
        webUi_url_ = "";
        return;
    }

    // 检查有无 clash_api 键
    const auto& experimental = json["experimental"];
    if(!experimental.HasMember("clash_api") ||
       !experimental["clash_api"].IsObject()) {
        webUi_url_ = "";
        return;
    }

    // 检查有无 external_controller 键
    const auto& clash_api = experimental["clash_api"];
    if(!clash_api.HasMember("external_controller") ||
       !clash_api["external_controller"].IsString()) {
        webUi_url_ = "";
        return;
    }

    webUi_url_ =
            format("http://{}", clash_api["external_controller"].GetString());
}

const rapidjson::Value* Config::find_profile(string_view profile_name) const {
    const auto& profiles = (*json_)["profiles"].GetArray();

    const auto profile =
            ranges::find_if(profiles, [profile_name](const auto& profile) {
                return profile["name"].GetString() == profile_name;
            });

    return profile != profiles.end() ? *&profile : nullptr;
}

// 字段检查辅助函数
template<typename TypeChecker>
static void check_field(const rapidjson::Value& object, string_view field,
                        TypeChecker checker, string_view error_prefix = "") {
    // 检查 json
    if(!object.HasMember(field.data())) {
        constexpr auto missing_key_format = "Missing required field: {}{}";
        throw json_field_error(format(missing_key_format, error_prefix, field));
    }
    if(!checker(object[field.data()])) {
        constexpr auto ket_incorrect_format = "Field '{}{}' has incorrect type";
        throw json_field_error(format(ket_incorrect_format, error_prefix, field));
    }
}

auto is_string_type = [](const auto& v) { return v.IsString(); };
auto is_bool_type = [](const auto& v) { return v.IsBool(); };
auto is_object_type = [](const auto& v) { return v.IsObject(); };
auto is_array_type = [](const auto& v) { return v.IsArray(); };

void Config::validate_config_file() const {
    check_field(*json_, "lang", is_string_type);
    check_field(*json_, "ua", is_string_type);
    check_field(*json_, "block_network", is_bool_type);
    check_field(*json_, "kernel", is_object_type);
    check_field(*json_, "profiles", is_array_type);

    validate_kernel();    // 检查 kernel 字段
    validate_profiles();  // 检查 profile 字段
}

void Config::validate_kernel() const {
    const auto& kernel_object = (*json_)["kernel"];
    constexpr string_view prefix = "kernel.";

    check_field(kernel_object, "path", is_string_type, prefix);
    check_field(kernel_object, "command", is_string_type, prefix);
    check_field(kernel_object, "config_path", is_string_type, prefix);
}

void Config::validate_profiles() const {
    const auto& profiles = (*json_)["profiles"];
    constexpr string_view error_prefix = "profile[{}].";

    for(rapidjson::SizeType i = 0; i < profiles.Size(); ++i) {
        const auto& profile = profiles[i];
        string prefix = format(error_prefix, to_string(i));

        // 检查 profile 数组中的各个对象
        check_field(profile, "name", is_string_type, prefix);
        check_field(profile, "path", is_string_type, prefix);
        check_field(profile, "url", is_string_type, prefix);
    }
}
