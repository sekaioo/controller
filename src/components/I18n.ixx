module;
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <string>

#include "constants.h"

export module components.I18n;

import common.Utils;
import components.Config;

using namespace std;
namespace fs = std::filesystem;

// @formatter:off
export class I18n {
public:
    static I18n& instance();
    void initialize(string_view code);
    [[nodiscard]] string get(string_view key) const;
    I18n(const I18n&) = delete;
    I18n& operator=(const I18n&) = delete;
    I18n(I18n&&) = delete;
    I18n& operator=(I18n&&) = delete;
private:
    I18n() :
        json_(make_unique<rapidjson::Document>()) {
        json_->SetObject();
    }
    ~I18n() = default;
    unique_ptr<rapidjson::Document> json_;
    static constexpr string_view file_format = "{}{}.json";
    static constexpr string_view missing_key_format_ = "MISSING KEY: {}";
};
// @formatter:on

export inline string tr(string_view key) {
    return I18n::instance().get(key);
}

export inline wstring wtr(string_view key) {
    return utf8_to_wide(I18n::instance().get(key));
}

I18n& I18n::instance() {
    static I18n instance;
    return instance;
}

void I18n::initialize(string_view code) {
    const fs::path file_name = exe_relative_path(format(file_format, LANG_DIR, code));

    ifstream file(file_name);
    if(!file.is_open())
        throw runtime_error("Could not open file " + file_name.string());

    rapidjson::IStreamWrapper isw(file);
    json_->ParseStream(isw);
    if(json_->HasParseError())
        throw runtime_error("JSON parse error");
    if(!json_->IsObject())
        throw runtime_error("JSON Format error");
}

string I18n::get(string_view key) const {
    // rapidjson 是 C 风格接口, 需要 \0 结尾, 在边界处构造 C 字符串
    const string key_str(key);
    if(json_->HasMember(key_str.c_str()))
        if(const auto& value = (*json_)[key_str.c_str()]; value.IsString())
            return value.GetString();

    return format(missing_key_format_, key);
}
