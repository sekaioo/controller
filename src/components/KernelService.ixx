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
import components.NetworkBlocker;
import common.Utils;
import common.Common;

using namespace std;
namespace fs = std::filesystem;

export class KernelService {
public:
    // @formatter:off
    KernelService() :
        kernel_path_(utf8_to_wide(Config::instance().get_kernel_path())),
        kernel_command_(utf8_to_wide(Config::instance().get_kernel_command())) {}
    ~KernelService() { stop(); }

    bool start();
    bool stop();

    bool is_running() const { return process_handle_.load() != nullptr; }
    HANDLE get_process_handle() const { return process_handle_.load(); }
    void register_observer(HWND main_window) { main_window_ = main_window; }
    // @formatter:on

private:
    void monitor_process(stop_token st);

    const wstring kernel_path_;
    const wstring kernel_command_;
    atomic<HANDLE> process_handle_ = nullptr;
    jthread monitor_thread_;
    HWND main_window_ = nullptr;
};

bool KernelService::start() {
    if(is_running()) return true;

    static const wstring executable_dir = get_executable_directory();
    static const wstring kernel_path = format(L"{}/{}", executable_dir, kernel_path_);
    static const wstring service_dir = fs::path(kernel_path).parent_path().wstring();

    HANDLE raw_handle = launch_hidden_process(
        kernel_command_.c_str(), kernel_path.c_str(), service_dir.c_str()
    );

    if(!raw_handle) {
        NetworkBlocker::instance().block_network();
        return false;
    }

    process_handle_.store(raw_handle);
    monitor_thread_ = jthread([this](stop_token st) { monitor_process(std::move(st)); });
    NetworkBlocker::instance().unblock_network();
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

static void stop_process_gracefully(HANDLE process);

void KernelService::monitor_process(stop_token st) {
    HANDLE proc = process_handle_.load();
    if(!proc) return;

    HandleGuard stop_event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if(!stop_event.h) return;

    stop_callback callback(st, [&] { SetEvent(stop_event.h); });
    const array handles = {proc, stop_event.h};
    const DWORD wait_result =
            WaitForMultipleObjects(handles.size(), handles.data(), FALSE, INFINITE);

    switch(wait_result) {
        case WAIT_OBJECT_0 + 0:
            if(main_window_)
                PostMessageW(main_window_, WM_KERNEL_TERMINATED, 0, 0);
            break;

        case WAIT_OBJECT_0 + 1:
            stop_process_gracefully(proc);
            break;

        default:
            break;
    }

    NetworkBlocker::instance().block_network();
    if(HANDLE h = process_handle_.exchange(nullptr))
        CloseHandle(h);
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
