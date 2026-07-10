module;
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>

export module components.Config;

import components.Logger;

using namespace std;

// @formatter:off
export class Config {
public:
    string lang;
    string ua;
    bool block_network;
    logger::Level log_level = logger::ALL;
    class Kernel {
    public:
        string path;
        string command;
        string config_path;
    } kernel;
    class Profile {
    public:
        string path;
        string url;
    };
    map<string, Profile> profiles;
    void load(const string& filename);
    void populate(const rapidjson::Document& doc);
private:
    static void validate_config(const rapidjson::Document& doc);
    static void validate_kernel(const rapidjson::Document& doc);
    static void validate_profiles(const rapidjson::Document& doc);
};
// @formatter:on

void Config::load(const string& filename) {
    ifstream file(filename);
    if(!file.is_open())
        throw runtime_error("open config file failed");

    rapidjson::Document json;
    rapidjson::IStreamWrapper isw(file);
    json.ParseStream(isw);
    if(json.HasParseError())
        throw runtime_error("JSON parse error");

    validate_config(json);
    populate(json);
}

// 解析日志等级名, 非法值抛出异常
static logger::Level parse_log_level(const string& name) {
    if(name == "ALL") return logger::ALL;
    if(name == "INFO") return logger::INFO;
    if(name == "WARN") return logger::WARN;
    if(name == "ERROR") return logger::ERROR;
    if(name == "FATAL") return logger::FATAL;
    if(name == "OFF") return logger::OFF;
    throw runtime_error(format("Field 'log_level' has invalid value: {}", name));
}

void Config::populate(const rapidjson::Document& doc) {
    lang = doc["lang"].GetString();
    ua = doc["ua"].GetString();
    block_network = doc["block_network"].GetBool();
    // log_level 为可选字段, 缺省时记录全部等级
    if(doc.HasMember("log_level"))
        log_level = parse_log_level(doc["log_level"].GetString());
    // kernel
    auto tk = doc["kernel"].GetObject();
    kernel.path = tk["path"].GetString();
    kernel.command = tk["command"].GetString();
    kernel.config_path = tk["config_path"].GetString();
    // profiles
    const auto& prof = doc["profiles"].GetObject();
    for(auto it = prof.MemberBegin(); it != prof.MemberEnd(); ++it) {
        Profile tp;
        tp.path = it->value["path"].GetString();
        tp.url = it->value["url"].GetString();
        profiles[it->name.GetString()] = tp;
    }
}

// 字段检查辅助函数
template<typename TypeChecker>
static void check_field(const rapidjson::Value& object, string_view field, TypeChecker checker,
                        string_view error_prefix = "") {
    // rapidjson 是 C 风格接口, 需要 \0 结尾, 在边界处构造 C 字符串
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

void Config::validate_config(const rapidjson::Document& doc) {
    check_field(doc, "lang", is_string_type);
    check_field(doc, "ua", is_string_type);
    check_field(doc, "block_network", is_bool_type);
    check_field(doc, "kernel", is_object_type);
    check_field(doc, "profiles", is_object_type);
    // 可选字段, 存在时校验类型
    if(doc.HasMember("log_level"))
        check_field(doc, "log_level", is_string_type);

    validate_kernel(doc);
    validate_profiles(doc);
}

void Config::validate_kernel(const rapidjson::Document& doc) {
    const auto& kernel_object = doc["kernel"];
    constexpr string_view prefix = "kernel.";

    check_field(kernel_object, "path", is_string_type, prefix);
    check_field(kernel_object, "command", is_string_type, prefix);
    check_field(kernel_object, "config_path", is_string_type, prefix);
}

void Config::validate_profiles(const rapidjson::Document& doc) {
    const auto& profiles = doc["profiles"];

    for(auto it = profiles.MemberBegin(); it != profiles.MemberEnd(); ++it) {
        const string tag = it->name.GetString();
        const auto& profile = it->value;
        const string prefix = format("profiles.{}.", tag);

        check_field(profile, "path", is_string_type, prefix);
        check_field(profile, "url", is_string_type, prefix);
    }
}
