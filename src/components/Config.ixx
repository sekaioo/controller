module;
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>

export module components.Config;

using namespace std;
namespace json = rapidjson;
namespace fs = std::filesystem;

// @formatter:off
export class Config {
public:
    string lang;
    string ua;
    bool block_network;
    struct Log {
        bool disabled;
        string level;
        string output;
        bool timestamp;
    } log;
    struct Kernel {
        string path;
        string command;
        string config_path;
    } kernel;
    struct Profile {
        string path;
        string url;
    };
    map<string, Profile> profiles;
    void load(const fs::path& filename);
    void populate(const json::Document& doc);
private:
    static void validate_config(const json::Document& doc);
    static void validate_log(const json::Document& doc);
    static void validate_kernel(const json::Document& doc);
    static void validate_profiles(const json::Document& doc);
};
// @formatter:on

void Config::load(const fs::path& filename) {
    ifstream file(filename);
    if(!file.is_open())
        throw runtime_error("open config file failed");

    json::Document json;
    json::IStreamWrapper isw(file);
    json.ParseStream(isw);
    if(json.HasParseError())
        throw runtime_error("JSON parse error");

    validate_config(json);
    populate(json);
}

void Config::populate(const json::Document& doc) {
    lang = doc["lang"].GetString();
    ua = doc["ua"].GetString();
    block_network = doc["block_network"].GetBool();

    // log
    auto log_object = doc["log"].GetObject();
    log.disabled = log_object["disabled"].GetBool();
    log.level = log_object["level"].GetString();
    log.output = log_object["output"].GetString();
    log.timestamp = log_object["timestamp"].GetBool();

    // kernel
    auto kernel_obj = doc["kernel"].GetObject();
    kernel.path = kernel_obj["path"].GetString();
    kernel.command = kernel_obj["command"].GetString();
    kernel.config_path = kernel_obj["config_path"].GetString();

    // profiles
    const auto& profiles_obj = doc["profiles"].GetObject();
    for(auto it = profiles_obj.MemberBegin(); it != profiles_obj.MemberEnd(); ++it) {
        Profile profile;
        profile.path = it->value["path"].GetString();
        profile.url = it->value["url"].GetString();
        profiles[it->name.GetString()] = profile;
    }
}

// 字段检查辅助函数
template<typename TypeChecker>
static void check_field(const json::Value& object, string_view field, TypeChecker checker,
                        string_view error_prefix = "") {
    const string field_str(field);
    if(!object.HasMember(field_str.c_str())) {
        constexpr auto missing_key_format = "Missing required field: {}{}";
        throw runtime_error(format(missing_key_format, error_prefix, field));
    }
    if(!checker(object[field_str.c_str()])) {
        constexpr auto key_incorrect_format = "Field '{}{}' has incorrect type";
        throw runtime_error(format(key_incorrect_format, error_prefix, field));
    }
}

auto is_string_type = [](const auto& v) { return v.IsString(); };
auto is_bool_type = [](const auto& v) { return v.IsBool(); };
auto is_object_type = [](const auto& v) { return v.IsObject(); };
auto is_array_type = [](const auto& v) { return v.IsArray(); };

void Config::validate_config(const json::Document& doc) {
    // top field
    check_field(doc, "lang", is_string_type);
    check_field(doc, "ua", is_string_type);
    check_field(doc, "block_network", is_bool_type);
    check_field(doc, "log", is_object_type);
    check_field(doc, "kernel", is_object_type);
    check_field(doc, "profiles", is_object_type);

    // object field
    validate_kernel(doc);
    validate_profiles(doc);
    validate_log(doc);
}

void Config::validate_log(const json::Document& doc) {
    const auto& kernel_object = doc["log"];
    constexpr string_view prefix = "log.";

    check_field(kernel_object, "disabled", is_bool_type, prefix);
    check_field(kernel_object, "level", is_string_type, prefix);
    check_field(kernel_object, "output", is_string_type, prefix);
    check_field(kernel_object, "timestamp", is_bool_type, prefix);
}


void Config::validate_kernel(const json::Document& doc) {
    const auto& kernel_object = doc["kernel"];
    constexpr string_view prefix = "kernel.";

    check_field(kernel_object, "path", is_string_type, prefix);
    check_field(kernel_object, "command", is_string_type, prefix);
    check_field(kernel_object, "config_path", is_string_type, prefix);
}

void Config::validate_profiles(const json::Document& doc) {
    const auto& profiles = doc["profiles"];

    for(auto it = profiles.MemberBegin(); it != profiles.MemberEnd(); ++it) {
        const string tag = it->name.GetString();
        const auto& profile = it->value;
        const string prefix = format("profiles.{}.", tag);

        check_field(profile, "path", is_string_type, prefix);
        check_field(profile, "url", is_string_type, prefix);
    }
}
