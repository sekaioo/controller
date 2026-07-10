module;
#include <windows.h>

#include <array>
#include <atomic>
#include <filesystem>
#include <format>
#include <string>
#include <thread>

#include "resource.h"

export module components.KernelService;

import components.Config;
import components.Logger;
import components.NetworkBlocker;
import common.Common;
import common.Utils;

using namespace std;
namespace fs = std::filesystem;

// @formatter:off
export class KernelService {
public:
    KernelService() = default;
    ~KernelService() { stop(); }
    bool start(wstring&& kernel_path, wstring&& kernel_command);
    bool stop();
    bool is_running() const { return process_handle_.load() != nullptr; }
    HANDLE get_process_handle() const { return process_handle_.load(); }
    void register_observer(HWND main_window) { main_window_ = main_window; }
private:
    static HANDLE create_kill_on_close_job();
    void monitor_process(stop_token st);

    atomic<HANDLE> process_handle_ = nullptr;
    jthread monitor_thread_;
    HWND main_window_ = nullptr;
    HandleGuard job_{create_kill_on_close_job()};
};
// @formatter:on

// 创建"句柄关闭即终止所有关联进程"的作业对象。内核进程加入后,
// 本程序无论以何种方式退出(正常退出、崩溃、被强杀), 系统关闭该句柄时都会终止内核
HANDLE KernelService::create_kill_on_close_job() {
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if(!job) return nullptr;

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
        CloseHandle(job);
        return nullptr;
    }
    return job;
}

bool KernelService::start(wstring&& kernel_path, wstring&& kernel_command) {
    if(is_running()) return true;

    const wstring executable_dir = get_executable_directory();
    const wstring full_kernel_path = format(L"{}/{}", executable_dir, kernel_path);
    const wstring service_dir = fs::path(full_kernel_path).parent_path().wstring();

    HANDLE raw_handle = launch_hidden_process(
        kernel_command.c_str(), full_kernel_path.c_str(), service_dir.c_str(), job_.h
    );

    if(!raw_handle) {
        logger::error(format("launch kernel failed, GetLastError={}", GetLastError()));
        network_blocker::block();
        return false;
    }

    logger::info("kernel started");
    process_handle_.store(raw_handle);
    monitor_thread_ = jthread([this](stop_token st) { monitor_process(std::move(st)); });
    network_blocker::unblock();
    return true;
}

bool KernelService::stop() {
    if(!is_running()) return true;

    if(monitor_thread_.joinable()) {
        monitor_thread_.request_stop();
        monitor_thread_.join();
        return true;
    }
    return false;
}

static void stop_process_gracefully(HANDLE process) {
    const DWORD pid = GetProcessId(process);
    if(!AttachConsole(pid)) return;

    SetConsoleCtrlHandler(nullptr, TRUE);
    GenerateConsoleCtrlEvent(CTRL_C_EVENT, pid);
    SetConsoleCtrlHandler(nullptr, FALSE);
    FreeConsole();

    // 等待退出，超时则强制终止
    if(WaitForSingleObject(process, 8000) == WAIT_TIMEOUT) {
        TerminateProcess(process, 0);
        WaitForSingleObject(process, 2000);
    }
}

void KernelService::monitor_process(stop_token st) {
    HANDLE proc = process_handle_.load();
    if(!proc) return;

    HandleGuard stop_event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if(!stop_event.h) return;

    stop_callback callback(st, [&] { SetEvent(stop_event.h); });
    const array handles = {proc, stop_event.h};
    const DWORD wait_result = WaitForMultipleObjects(
            static_cast<DWORD>(handles.size()), handles.data(), FALSE, INFINITE);

    switch(wait_result) {
        case WAIT_OBJECT_0 + 0:
            logger::error("kernel exited unexpectedly");
            if(main_window_)
                PostMessageW(main_window_, WM_KERNEL_TERMINATED, 0, 0);
            break;

        case WAIT_OBJECT_0 + 1:
            stop_process_gracefully(proc);
            logger::info("kernel stopped");
            break;

        default:
            break;
    }

    network_blocker::block();
    if(HANDLE h = process_handle_.exchange(nullptr))
        CloseHandle(h);
}
