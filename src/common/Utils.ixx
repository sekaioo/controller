module;
#include <Windows.h>

#include <filesystem>
#include <string>
#include <vector>
export module common.Utils;

using wstring = std::wstring;
using string_view = std::string_view;
template<typename T>
using vector = std::vector<T>;

// utf-8 to utf-16
export wstring utf8_to_wide(string_view utf8_str) {
    const int wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8_str.data(), utf8_str.size(), nullptr, 0);
    if(wide_len == 0) return wstring{};

    wstring wide_str;
    wide_str.resize(wide_len);
    MultiByteToWideChar(CP_UTF8, 0, utf8_str.data(), static_cast<int>(utf8_str.size()), wide_str.data(), wide_len);
    return wide_str;
}

// 启动隐藏进程
export HANDLE launch_hidden_process(
    const wchar_t* command,
    const wchar_t* application = nullptr,  // lpApplicationName
    const wchar_t* working_dir = nullptr)  // lpCurrentDirectory
{
    STARTUPINFOW si = {sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    constexpr DWORD flags = CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT;
    if(!CreateProcessW(application, const_cast<LPWSTR>(command), nullptr, nullptr, FALSE,
                       flags, nullptr, working_dir, &si, &pi))
        return nullptr;

    CloseHandle(pi.hThread);
    return pi.hProcess;
}

// 取程序路径
export wstring get_executable_directory() {
    vector<wchar_t> buffer(MAX_PATH);
    DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));

    while(size == buffer.size() && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        buffer.resize(buffer.size() * 2);
        size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }

    if(size == 0)return L"";

    std::filesystem::path full_path(buffer.data());
    return full_path.parent_path().wstring();
}

// 打开 URL
export void open_url(string_view url) {
    auto open_url = utf8_to_wide(url);
    ShellExecuteW(nullptr, L"open", open_url.c_str(), nullptr, nullptr,SW_SHOWNORMAL);
}
