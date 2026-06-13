module;
#include <windows.h>

#include <format>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "constants.h"
#include "resource.h"

export module components.Service;

import components.Config;
import components.I18n;
import components.KernelService;
import components.NetworkBlocker;
import components.TrayManager;
import common.Utils;
import profile.Manager;
import profile.Downloader;

using namespace std;

// @formatter:off
export class Service {
public:
    explicit Service(HINSTANCE instance_handle, Config& config) :
        config_ (config),
        main_window_(nullptr),
        instance_handle_(instance_handle),
        service_(make_shared<KernelService>()),
        tray_manager_(make_unique<TrayManager>(service_)) {
        initialize();
    }
    ~Service() = default;
    bool initialize();

    static void run();
    static LRESULT CALLBACK window_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handle_message(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) const;
private:
    // 处理菜单命令
    void handle_menu_command(int menuId) const;
    void on_switch_profile(int profile_index) const;
    void on_update_profiles() const;
    void on_start_service() const;
    void on_stop_service() const;
    void on_exit() const;
    string pop_error_message() const;

    Config& config_;
    HWND main_window_;
    HINSTANCE instance_handle_;
    shared_ptr<KernelService> service_;
    unique_ptr<TrayManager> tray_manager_;
    mutable queue<string> error_queue_;
    mutable mutex error_queue_mutex_;
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
    service_->register_observer(main_window_);
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

LRESULT Service::handle_message(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) const {
    // explorer 崩溃时刷新托盘
    static UINT WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");
    if(msg == WM_TASKBARCREATED) {
        tray_manager_->refresh_tray();
        return 0;
    }

    switch(msg) {
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
                tray_manager_->show_menu();
            break;

        // 处理菜单命令
        case WM_COMMAND:
            handle_menu_command(LOWORD(wParam));
            break;

        // 内核停止运行
        case WM_KERNEL_TERMINATED:
            if(config_.block_network) {
                wstring err_msg = format(L"{}, {}", wtr("dialog.kernel_stopped"),
                                         wtr("dialog.network_blocked"));
                MessageBoxW(nullptr, err_msg.c_str(),
                            wtr("dialog.hint").c_str(),MB_ICONINFORMATION);
            } else
                MessageBoxW(nullptr, wtr("dialog.kernel_stopped").c_str(),
                            wtr("dialog.hint").c_str(), MB_ICONINFORMATION);
            break;

        // 订阅更新完成
        case WM_PROFILE_UPDATE_COMPLETE:
            MessageBoxW(nullptr, wtr("dialog.update_profile_complete").c_str(),
                        wtr("dialog.complete").c_str(), MB_ICONINFORMATION);
            break;

        // 订阅更新错误
        case WM_PROFILE_UPDATE_ERROR: {
            const wstring err_msg = utf8_to_wide(pop_error_message());
            if(!err_msg.empty())
                MessageBoxW(nullptr, err_msg.c_str(),
                            wtr("dialog.error").c_str(), MB_ICONERROR);
            break;
        }

        case WM_CLOSE:
            service_->stop();
            tray_manager_->cleanup();
            DestroyWindow(hWnd);
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProcW(hWnd, msg, wParam, lParam);
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
    } catch(std::exception&) {
        on_stop_service();
    }
}

void Service::on_switch_profile(const int profile_index) const {
    if(profile_index >= 0 && static_cast<size_t>(profile_index) < ProfileManager::profiles_names.size()) {
        const bool is_running = service_->is_running();

        if(is_running) on_stop_service();
        try {
            string profile_name = ProfileManager::get_profile_name(profile_index);
            string path = config_.profiles[profile_name].path;
            string kernel_config_path = config_.kernel.config_path;
            ProfileManager::switch_profile(path, kernel_config_path);
        } catch(const runtime_error& e) {
            const wstring msg = utf8_to_wide(e.what());
            MessageBoxW(nullptr, msg.c_str(), wtr("dialog.path_error").c_str(),MB_ICONERROR);
        }
        if(is_running)
            on_start_service();
    }
}

void Service::on_update_profiles() const {
    // 启动一个后台线程来执行更新任务
    thread([this] {
        for(const auto names = ProfileManager::profiles_names;
            const auto& name : names) {
            string ua = config_.ua;
            string url = config_.profiles[name].url;
            string path = config_.profiles[name].path;
            if(!ProfileManager::update_profile(url, ua, path)) {
                {
                    lock_guard lock(error_queue_mutex_);
                    error_queue_.push(name);
                }
                PostMessageW(main_window_, WM_PROFILE_UPDATE_ERROR, 0, 0);
            }
        }
        PostMessageW(main_window_, WM_PROFILE_UPDATE_COMPLETE, 0, 0);
    }).detach();
}

void Service::on_start_service() const {
    if(!service_->start(utf8_to_wide(config_.kernel.path), utf8_to_wide(config_.kernel.command)))
        MessageBoxW(nullptr, wtr("dialog.start_kernel_failed").c_str(),
                    wtr("dialog.error").c_str(), MB_ICONERROR);
}

void Service::on_stop_service() const {
    if(!service_->stop())
        MessageBoxW(nullptr, wtr("dialog.stop_kernel_failed").c_str(),
                    wtr("dialog.error").c_str(), MB_ICONERROR);
}

void Service::on_exit() const {
    PostMessageW(main_window_, WM_CLOSE, 0, 0);
}

string Service::pop_error_message() const {
    lock_guard lock(error_queue_mutex_);
    if(error_queue_.empty()) return {};
    string msg = move(error_queue_.front());
    error_queue_.pop();
    return msg;
}
