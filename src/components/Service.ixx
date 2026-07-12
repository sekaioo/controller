module;
#include <windows.h>

#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "constants.h"
#include "resource.h"

// windows.h 定义的 ERROR 宏与 Log::ERROR 冲突
#ifdef ERROR
#undef ERROR
#endif

export module components.Service;

import components.Config;
import components.I18n;
import components.KernelService;
import components.Logger;
import components.NetworkBlocker;
import components.TrayManager;
import common.Utils;
import profile.Manager;

using namespace std;

// @formatter:off
export class Service {
public:
    explicit Service(HINSTANCE instance_handle, Config& config) :
        config_ (config),
        main_window_(nullptr),
        instance_handle_(instance_handle),
        profiles_manager_(config),
        kernel_service_(make_shared<KernelService>()),
        tray_manager_(make_unique<TrayManager>(kernel_service_, profiles_manager_.names())),
        wm_taskbarcreated_(RegisterWindowMessageW(L"TaskbarCreated")) {
        if(!initialize())
            throw runtime_error("Service initialization failed");
    }
    ~Service() = default;

    void run();
    static LRESULT CALLBACK window_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handle_message(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) const;
private:
    bool initialize();
    void handle_menu_command(int menuId) const;
    void on_switch_profile(int profile_index) const;
    void on_update_profiles() const;
    void on_start_service() const;
    void on_stop_service() const;
    void on_exit() const;

    Config& config_;
    HWND main_window_;
    HINSTANCE instance_handle_;
    mutable ProfileManager profiles_manager_;
    shared_ptr<KernelService> kernel_service_;
    unique_ptr<TrayManager> tray_manager_;
    UINT wm_taskbarcreated_;
};
// @formatter:on

bool Service::initialize() {
    // 注册窗口类
    WNDCLASSEXW window_class = {sizeof(WNDCLASSEXW)};
    window_class.style = 0;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance_handle_;
    window_class.hIcon = LoadIconW(instance_handle_, MAKEINTRESOURCEW(IDI_APP_ICON));
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = nullptr;
    window_class.lpszClassName = WINDOWS_CLASS_NAME;
    RegisterClassExW(&window_class);

    // 创建主窗口
    main_window_ = CreateWindowExW(0, WINDOWS_CLASS_NAME, nullptr, WS_POPUP, 0, 0, 0, 0,
                                   nullptr, nullptr, instance_handle_, this);

    if(!main_window_) return false;

    // 初始化托盘图标
    tray_manager_->initialize(main_window_, instance_handle_);

    // 注册 kernel 服务的观察者
    kernel_service_->register_observer(main_window_);
    on_start_service();
    return true;
}

void Service::run() {
    MSG msg;
    while(GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

LRESULT CALLBACK Service::window_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Service* pController = nullptr;

    if(msg == WM_NCCREATE) {
        const CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pController = static_cast<Service*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pController));
    } else
        pController = reinterpret_cast<Service*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    if(pController)
        return pController->handle_message(hWnd, msg, wParam, lParam);
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT Service::handle_message(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) const {
    // explorer 崩溃时刷新托盘
    if(message == wm_taskbarcreated_) {
        tray_manager_->refresh_tray();
        return 0;
    }
    switch(message) {
        case WM_CREATE:
            SetTimer(hWnd, 1, 2000, nullptr);
            break;

        case WM_TIMER:
            if(tray_manager_->refresh_tray())
                KillTimer(hWnd, 1);
            break;

        // 托盘图标消息
        case WM_SHOW_MENU:
            if(lParam == WM_LBUTTONUP || lParam == WM_RBUTTONUP)
                tray_manager_->show_menu(profiles_manager_.current_index(), profiles_manager_.is_updating());
            break;

        // 处理菜单命令
        case WM_COMMAND:
            handle_menu_command(LOWORD(wParam));
            break;

        // 内核停止运行
        case WM_KERNEL_TERMINATED: {
            wstring msg = config_.block_network ?
                              format(L"{}, {}", wtr("dialog.kernel_stopped"), wtr("dialog.network_blocked")) :
                              wtr("dialog.kernel_stopped");
            const wstring title = wtr("dialog.hint");
            MessageBoxW(nullptr, msg.c_str(), title.c_str(),MB_ICONINFORMATION);
        }
        break;

        // 订阅更新完成, 依结果提示: 成功报完成, 有失败列出失败项, 正在更新则忽略
        case WM_PROFILE_UPDATE_COMPLETE:
            switch(static_cast<ProfileManager::UpdateResult>(wParam)) {
                case ProfileManager::UpdateResult::Busy:
                    break;
                case ProfileManager::UpdateResult::Success: {
                    const wstring msg = wtr("dialog.update_profile_complete");
                    const wstring title = wtr("dialog.complete");
                    MessageBoxW(nullptr, msg.c_str(), title.c_str(), MB_ICONINFORMATION);
                    break;
                }
                case ProfileManager::UpdateResult::Failed: {
                    const wstring msg = format(L"{}\n{}", wtr("dialog.update_profile_failed"),
                                               utf8_to_wide(profiles_manager_.take_errors()));
                    const wstring title = wtr("dialog.error");
                    MessageBoxW(nullptr, msg.c_str(), title.c_str(), MB_ICONERROR);
                    break;
                }
            }
            break;

        case WM_CLOSE:
            kernel_service_->stop();
            tray_manager_->cleanup();
            DestroyWindow(hWnd);
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

void Service::handle_menu_command(const int menuId) const {
    // 处理订阅切换
    try {
        if(menuId >= IDM_PROFILE_BASE && menuId < IDM_PROFILE_MAX)
            on_switch_profile(menuId - IDM_PROFILE_BASE);
        else {
            switch(menuId) {
                case IDM_UPDATE_PROFILE:
                    on_update_profiles();
                    break;
                case IDM_START_KERNEL:
                    on_start_service();
                    break;
                case IDM_STOP_KERNEL:
                    on_stop_service();
                    break;
                case IDM_APP_EXIT:
                    on_exit();
                    break;
                default:
                    break;
            }
        }
    } catch(const std::exception& e) {
        Logger::log(e.what(), Logger::ERROR);
        const wstring msg = utf8_to_wide(e.what());
        MessageBoxW(nullptr, msg.c_str(), wtr("dialog.error").c_str(), MB_ICONERROR);
    }
}

void Service::on_switch_profile(const int profile_index) const {
    if(profile_index < 0 || static_cast<size_t>(profile_index) >= profiles_manager_.count())
        return;

    const bool is_running = kernel_service_->is_running();
    if(is_running) on_stop_service();
    try {
        profiles_manager_.switch_profile(profile_index);
    } catch(const runtime_error& e) {
        Logger::log(e.what(), Logger::ERROR);
        const wstring msg = utf8_to_wide(e.what());
        MessageBoxW(nullptr, msg.c_str(), wtr("dialog.path_error").c_str(),MB_ICONERROR);
    }
    if(is_running) on_start_service();
}

void Service::on_update_profiles() const {
    profiles_manager_.update_all_profile([window = main_window_](ProfileManager::UpdateResult result) {
        PostMessageW(window, WM_PROFILE_UPDATE_COMPLETE, static_cast<WPARAM>(result), 0);
    });
}

void Service::on_start_service() const {
    if(!kernel_service_->start(utf8_to_wide(config_.kernel.path), utf8_to_wide(config_.kernel.command))) {
        const wstring msg = wtr("dialog.start_kernel_failed");
        const wstring title = wtr("dialog.error");
        MessageBoxW(nullptr, msg.c_str(), title.c_str(), MB_ICONERROR);
    }
}

void Service::on_stop_service() const {
    if(!kernel_service_->stop()) {
        const wstring msg = wtr("dialog.stop_kernel_failed");
        const wstring title = wtr("dialog.error");
        MessageBoxW(nullptr, msg.c_str(), title.c_str(), MB_ICONERROR);
    }
}

void Service::on_exit() const {
    PostMessageW(main_window_, WM_CLOSE, 0, 0);
}
