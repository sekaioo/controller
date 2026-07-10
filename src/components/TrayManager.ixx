module;
#include <windows.h>

#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "resource.h"

export module components.TrayManager;

import components.KernelService;
import components.I18n;
import common.Utils;
import profile.Manager;

using namespace std;

// @formatter:off
export class TrayManager {
public:
    explicit TrayManager(shared_ptr<KernelService> service) :
        service_(move(service)) {}
    bool initialize(HWND main_window, HINSTANCE instance_handle);

    bool register_tray();
    bool refresh_tray();
    void show_menu() const;
    void cleanup();
private:
    NOTIFYICONDATAW tray_icon_ = {sizeof(NOTIFYICONDATAW)};
    HWND main_window_ = nullptr;
    HINSTANCE instance_handle_ = nullptr;
    shared_ptr<KernelService> service_ = nullptr;
};
// @formatter:on

bool TrayManager::initialize(HWND main_window, HINSTANCE instance_handle) {
    main_window_ = main_window;
    instance_handle_ = instance_handle;
    return register_tray();
}

bool TrayManager::register_tray() {
    tray_icon_.hWnd = main_window_;
    tray_icon_.uID = 1;
    tray_icon_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray_icon_.hIcon = LoadIconW(instance_handle_, MAKEINTRESOURCEW(IDI_APP_ICON));
    tray_icon_.uCallbackMessage = WM_SHOW_MENU;
    wcscpy_s(tray_icon_.szTip, L"Controller");
    return refresh_tray();
}

bool TrayManager::refresh_tray() {
    // 图标已存在时 NIM_ADD 会失败, 先尝试修改, 失败(图标不存在)再添加
    if(Shell_NotifyIconW(NIM_MODIFY, &tray_icon_)) return true;
    return Shell_NotifyIconW(NIM_ADD, &tray_icon_);
}

void TrayManager::show_menu() const {
    POINT pt;
    GetCursorPos(&pt);

    // 主菜单和订阅菜单
    HMENU hMenu = CreatePopupMenu();
    HMENU hSubMenu = CreatePopupMenu();

    // 填充订阅子菜单
    vector<string> names = ProfileManager::profiles_names;
    for(size_t i = 0; i < names.size() && i < IDM_PROFILE_MAX; ++i) {
        wstring name = utf8_to_wide(names[i]);
        AppendMenuW(hSubMenu, MF_STRING, IDM_PROFILE_BASE + i, name.c_str());
    }

    // 订阅相关
    AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hSubMenu), wtr("tray.switch_profile").c_str());
    AppendMenuW(hMenu, MF_STRING, IDM_UPDATE_PROFILE, wtr("tray.update_profile").c_str());
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // 启动 / 停止按钮状态
    if(service_->is_running()) {
        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, IDM_START_KERNEL, wtr("tray.start_kernel").c_str());
        AppendMenuW(hMenu, MF_STRING, IDM_STOP_KERNEL, wtr("tray.stop_kernel").c_str());
    } else {
        AppendMenuW(hMenu, MF_STRING, IDM_START_KERNEL, wtr("tray.start_kernel").c_str());
        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, IDM_STOP_KERNEL, wtr("tray.stop_kernel").c_str());
    }
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // 退出按钮
    AppendMenuW(hMenu, MF_STRING, IDM_APP_EXIT, wtr("tray.exit_app").c_str());
    SetForegroundWindow(main_window_);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, main_window_, nullptr);
    PostMessageW(main_window_, WM_NULL, 0, 0);

    DestroyMenu(hSubMenu);
    DestroyMenu(hMenu);
}

void TrayManager::cleanup() {
    Shell_NotifyIconW(NIM_DELETE, &tray_icon_);
}
