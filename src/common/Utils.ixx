module;
#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

export module common.Utils;

using wstring = std::wstring;
using string_view = std::string_view;
template<typename T>
using vector = std::vector<T>;

// utf-8 to utf-16
export wstring utf8_to_wide(string_view utf8_str) {
    const int wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8_str.data(),
                                             static_cast<int>(utf8_str.size()), nullptr, 0);
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
    const wchar_t* working_dir = nullptr,  // lpCurrentDirectory
    HANDLE job = nullptr)                  // 非空时进程在开始运行前被加入该作业对象
{
    STARTUPINFOW si = {sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    // 需要加入作业对象时先挂起创建, 避免进程在加入前逃逸
    DWORD flags = CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT;
    if(job) flags |= CREATE_SUSPENDED;
    if(!CreateProcessW(application, const_cast<LPWSTR>(command), nullptr, nullptr, FALSE,
                       flags, nullptr, working_dir, &si, &pi))
        return nullptr;

    if(job) {
        if(!AssignProcessToJobObject(job, pi.hProcess)) {
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return nullptr;
        }
        ResumeThread(pi.hThread);
    }

    CloseHandle(pi.hThread);
    return pi.hProcess;
}

// 按 Windows 命令行解析规则 (CommandLineToArgvW) 为参数加引号并转义
export wstring quote_argument(std::wstring_view arg) {
    wstring result = L"\"";
    size_t backslashes = 0;
    for(const wchar_t c : arg) {
        if(c == L'\\') {
            ++backslashes;
            continue;
        }
        if(c == L'"') {
            result.append(backslashes * 2 + 1, L'\\');
        } else
            result.append(backslashes, L'\\');
        result += c;
        backslashes = 0;
    }
    result.append(backslashes * 2, L'\\');
    result += L'"';
    return result;
}

// 取程序路径
wstring get_executable_directory() {
    vector<wchar_t> buffer;
    DWORD size;
    do {
        buffer.resize(buffer.empty() ? MAX_PATH : buffer.size() * 2);
        size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    } while(size == buffer.size() && GetLastError() == ERROR_INSUFFICIENT_BUFFER);
    if(size == 0) return L"";
    return std::filesystem::path(buffer.data(), buffer.data() + size).parent_path().wstring();
}

// 基于程序目录构造绝对路径 (relative 为 UTF-8)
export std::filesystem::path exe_relative_path(string_view relative) {
    static const std::filesystem::path base = get_executable_directory();
    return base / utf8_to_wide(relative);
}


// 读取整个文件内容, 打不开返回空
export std::string read_file_bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if(!file.is_open()) return {};
    return {std::istreambuf_iterator(file), std::istreambuf_iterator<char>()};
}
