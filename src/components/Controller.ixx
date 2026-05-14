module;
#include <windows.h>

#include <sstream>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "constants.h"
#include "resource.h"
export module components.Controller;

import components.Config;
import components.I18n;
import components.KernelService;
import components.NetworkBlocker;
import components.TrayManager;
import common.Utils;
import profile.Manager;
import profile.Downloader;

using namespace std;

export class Controller {
public:
    // 构造与析构函数
    explicit Controller(HINSTANCE instance_handle)
    : main_window_(nullptr),
      instance_handle_(instance_handle),
      service_(make_shared<KernelService>()),
      tray_manager_(make_unique<TrayManager>(service_)) {
        initialize();
    }

    ~Controller() = default;

    // 初始化
    bool initialize();

    // 主消息循环
    static void run();

    // 窗口过程
    static LRESULT CALLBACK window_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // 处理窗口消息
    LRESULT handle_message(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) const;

private:
    // 处理菜单命令
    void handle_menu_command(int menuId) const;

    // 消息处理辅助函数 @formatter:off
    void on_switch_profile(int profile_index) const;
    void on_update_profiles() const;
    void on_start_service() const;
    void on_stop_service() const;
    void on_exit() const;
    // @formatter:on

    HWND main_window_;                      // 主窗口句柄
    HINSTANCE instance_handle_;             // 实例句柄
    shared_ptr<KernelService> service_;     // 内核控制器
    unique_ptr<TrayManager> tray_manager_;  // 托盘管理器
};

bool Controller::initialize() {
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

void Controller::run() {
    MSG msg;
    while(GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

LRESULT CALLBACK Controller::window_proc(HWND hWnd, UINT msg, WPARAM wParam,
                                         LPARAM lParam) {
    Controller* pController = nullptr;

    if(msg == WM_NCCREATE) {
        const CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pController = static_cast<Controller*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pController));
    } else {
        pController = reinterpret_cast<Controller*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    if(pController) {
        return pController->handle_message(hWnd, msg, wParam, lParam);
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT Controller::handle_message(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) const {
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
            if(tray_manager_->refresh_tray()) {
                KillTimer(hWnd, 1);
            }
            break;

        // 托盘图标消息
        case WM_SHOW_MENU:
            if(const auto url = Config::instance().get_webUi_url();
                lParam == WM_LBUTTONUP && !url.empty()) {
                open_url(url);
            } else if(lParam == WM_RBUTTONUP) {
                tray_manager_->show_menu();
            }
            break;

        // 处理菜单命令
        case WM_COMMAND:
            handle_menu_command(LOWORD(wParam));
            break;

        // 内核停止运行
        case WM_KERNEL_TERMINATED:
            if(Config::instance().get_block_network()) {
                wstring err_msg = format(L"{}, {}", wtr("dialog.kernel_stopped"), wtr("dialog.network_blocked"));
                MessageBoxW(nullptr, err_msg.c_str(), wtr("dialog.hint").c_str(),MB_ICONINFORMATION);
            } else {
                MessageBoxW(nullptr, wtr("dialog.kernel_stopped").c_str(),
                            wtr("dialog.hint").c_str(), MB_ICONINFORMATION);
            }
            break;

        // 订阅更新完成
        case WM_PROFILE_UPDATE_COMPLETE:
            MessageBoxW(nullptr, wtr("dialog.update_profile_complete").c_str(),
                        wtr("dialog.complete").c_str(), MB_ICONINFORMATION);
            break;

        // 订阅更新错误
        case WM_PROFILE_UPDATE_ERROR:
            if(const auto* error_msg = reinterpret_cast<string*>(wParam)) {
                const wstring err_msg = utf8_to_wide(*error_msg);
                MessageBoxW(nullptr, err_msg.c_str(), wtr("dialog.error").c_str(),MB_ICONERROR);
                delete error_msg;
            }
            break;

        case WM_CLOSE:
            service_->stop();
            tray_manager_->cleanup();
            DestroyWindow(hWnd);

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

void Controller::handle_menu_command(const int menuId) const {
    // 处理订阅切换
    try {
        if(menuId >= IDM_PROFILE_BASE && menuId < IDM_PROFILE_MAX) {
            on_switch_profile(menuId - IDM_PROFILE_BASE);
        } else {
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

void Controller::on_switch_profile(const int profile_index) const {
    const vector<string> profile_name = ProfileManager::get_profile_names();

    if(profile_index < profile_name.size()) {
        const bool is_running = service_->is_running();

        // 如果服务正在运行，先停止它
        if(is_running) {
            on_stop_service();
        }
        try {
            ProfileManager::switch_profile(profile_name[profile_index]);
        } catch(const runtime_error& e) {
            const wstring msg = utf8_to_wide(e.what());
            MessageBoxW(nullptr, msg.c_str(), wtr("dialog.path_error").c_str(),MB_ICONERROR);
        }
        if(is_running) {
            on_start_service();
        }
    }
}

void Controller::on_update_profiles() const {
    // 启动一个后台线程来执行更新任务
    thread([this] {
        for(const auto names = ProfileManager::get_profile_names();
            const auto& name : names) {
            if(!ProfileManager::update_profile(name)) {
                // 更新错误发送消息
                auto* error_msg = new string(name);
                PostMessageW(main_window_, WM_PROFILE_UPDATE_ERROR, reinterpret_cast<WPARAM>(error_msg), 0);
            }
        }
        PostMessageW(main_window_, WM_PROFILE_UPDATE_COMPLETE, 0, 0);
    }).detach();
}

void Controller::on_start_service() const {
    if(!service_->start()) {
        MessageBoxW(nullptr, wtr("dialog.start_kernel_failed").c_str(),
                    wtr("dialog.error").c_str(), MB_ICONERROR);
    }
}

void Controller::on_stop_service() const {
    if(!service_->stop()) {
        MessageBoxW(nullptr, wtr("dialog.stop_kernel_failed").c_str(),
                    wtr("dialog.error").c_str(), MB_ICONERROR);
    }
}

void Controller::on_exit() const { PostMessageW(main_window_, WM_CLOSE, 0, 0); }
