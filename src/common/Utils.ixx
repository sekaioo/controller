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
