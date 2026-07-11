module;
#include <windows.h>

#include <atomic>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <queue>
#include <ranges>
#include <stdexcept>
#include <string>
#include <thread>
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
import profile.Downloader;

using namespace std;

// 订阅名列表按 config.profiles 的 key 顺序生成, 即菜单顺序
static vector<string> profile_names_from(const Config& config) {
    vector<string> names;
    names.reserve(config.profiles.size());
    for(const auto& name : config.profiles | views::keys)
        names.push_back(name);
    return names;
}

// 读取整个文件内容, 打不开返回空
static string read_file_bytes(const std::filesystem::path& path) {
    ifstream file(path, ios::binary);
    if(!file.is_open()) return {};
    return {istreambuf_iterator(file), istreambuf_iterator<char>()};
}

// 启动时通过内容比对推断当前生效的订阅, 无法确定时返回 -1
static int detect_current_profile(const Config& config, const vector<string>& names) {
    const string kernel_config = read_file_bytes(exe_relative_path(config.kernel.config_path));
    if(kernel_config.empty()) return -1;

    for(size_t i = 0; i < names.size(); ++i) {
        const auto it = config.profiles.find(names[i]);
        if(it == config.profiles.end()) continue;
        const auto profile_path = exe_relative_path(format("{}{}", PROFILES_DIR, it->second.path));
        if(read_file_bytes(profile_path) == kernel_config)
            return static_cast<int>(i);
    }
    return -1;
}

// @formatter:off
export class Service {
public:
    explicit Service(HINSTANCE instance_handle, Config& config) :
        config_ (config),
        main_window_(nullptr),
        instance_handle_(instance_handle),
        profile_names_(profile_names_from(config)),
        current_profile_index_(detect_current_profile(config, profile_names_)),
        service_(make_shared<KernelService>()),
        tray_manager_(make_unique<TrayManager>(service_, profile_names_)),
        wm_taskbarcreated_(RegisterWindowMessageW(L"TaskbarCreated")) {
        if(!initialize())
            throw runtime_error("Service initialization failed");
    }
    ~Service() = default;

    void run();
    static LRESULT CALLBACK window_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handle_message(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) const;
private:
    bool initialize();
    void handle_menu_command(int menuId) const;
    string pop_all_errors() const;
    void on_switch_profile(int profile_index) const;
    void on_update_profiles() const;
    void on_start_service() const;
    void on_stop_service() const;
    void on_exit() const;

    // 订阅更新的共享状态: 后台线程按值持有 shared_ptr,
    // 即使 Service 已析构线程也不会访问悬垂内存
    struct UpdateState {
        mutex mutex;
        queue<string> errors;
        atomic<bool> updating = false;
    };

    Config& config_;
    HWND main_window_;
    HINSTANCE instance_handle_;
    vector<string> profile_names_;
    mutable int current_profile_index_;
    shared_ptr<KernelService> service_;
    unique_ptr<TrayManager> tray_manager_;
    shared_ptr<UpdateState> update_state_ = make_shared<UpdateState>();
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
    if(msg == wm_taskbarcreated_) {
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
                tray_manager_->show_menu(current_profile_index_,
                                         update_state_->updating.load());
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

        // 订阅更新完成, 一次性汇总结果: 全部成功报完成, 有失败列出失败项
        case WM_PROFILE_UPDATE_COMPLETE: {
            const string errors = pop_all_errors();
            if(errors.empty())
                MessageBoxW(nullptr, wtr("dialog.update_profile_complete").c_str(),
                            wtr("dialog.complete").c_str(), MB_ICONINFORMATION);
            else {
                const wstring err_msg = format(L"{}\n{}", wtr("dialog.update_profile_failed"),
                                               utf8_to_wide(errors));
                MessageBoxW(nullptr, err_msg.c_str(),
                            wtr("dialog.error").c_str(), MB_ICONERROR);
            }
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
    } catch(const std::exception& e) {
        // 把错误展示给用户而不是静默吞掉
        MessageBoxW(nullptr, utf8_to_wide(e.what()).c_str(),
                    wtr("dialog.error").c_str(), MB_ICONERROR);
    }
}

string Service::pop_all_errors() const {
    lock_guard lock(update_state_->mutex);
    string result;
    while(!update_state_->errors.empty()) {
        if(!result.empty()) result += '\n';
        result += update_state_->errors.front();
        update_state_->errors.pop();
    }
    return result;
}

void Service::on_switch_profile(const int profile_index) const {
    if(profile_index >= 0 && static_cast<size_t>(profile_index) < profile_names_.size()) {
        const bool is_running = service_->is_running();

        if(is_running) on_stop_service();
        try {
            const string& profile_name = profile_names_[profile_index];
            switch_profile(config_.profiles.at(profile_name).path, config_.kernel.config_path);
            current_profile_index_ = profile_index;
        } catch(const runtime_error& e) {
            const wstring msg = utf8_to_wide(e.what());
            MessageBoxW(nullptr, msg.c_str(), wtr("dialog.path_error").c_str(),MB_ICONERROR);
        }
        if(is_running)
            on_start_service();
    }
}

void Service::on_update_profiles() const {
    if(update_state_->updating.exchange(true)) return;

    struct Task {
        string name, url, path;
    };
    vector<Task> tasks;
    tasks.reserve(config_.profiles.size());
    for(const auto& [name, profile] : config_.profiles)
        tasks.push_back({name, profile.url, profile.path});

    thread([tasks = std::move(tasks), ua = config_.ua,
        state = update_state_, window = main_window_] {
        const auto report_error = [&](string msg) {
            Logger::log_with_date_time(format("update profile failed: {}", msg), Logger::ERROR);
            lock_guard lock(state->mutex);
            state->errors.push(std::move(msg));
        };
        for(const auto& [name, url, path] : tasks) {
            try {
                if(!update_profile(url, ua, path))
                    report_error(name);
            } catch(const exception& e) {
                report_error(format("{}: {}", name, e.what()));
            } catch(...) {
                report_error(name);
            }
        }
        state->updating.store(false);
        PostMessageW(window, WM_PROFILE_UPDATE_COMPLETE, 0, 0);
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
