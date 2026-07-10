#pragma once

#define IDI_APP_ICON 101  // 定义图标资源 ID

#define WM_SHOW_MENU (WM_APP + 100)                // 显示托盘 ID
#define WM_KERNEL_TERMINATED (WM_APP + 101)        // kernel 进程终止消息 ID
#define WM_PROFILE_UPDATE_COMPLETE (WM_APP + 102)  // 更新结束 ID

// 菜单项 ID
#define IDM_START_KERNEL (WM_APP + 1000)    // 启动 kernel ID
#define IDM_STOP_KERNEL (WM_APP + 1001)     // 停止 kernel ID
#define IDM_SWITCH_PROFILE (WM_APP + 1002)  // 切换订阅 ID
#define IDM_UPDATE_PROFILE (WM_APP + 1003)  // 更新订阅 ID
#define IDM_APP_EXIT (WM_APP + 1004)        // 退出 ID

// 订阅菜单项的 ID 范围
#define IDM_PROFILE_BASE (WM_APP + 2000)
#define IDM_PROFILE_MAX (WM_APP + 2100)
