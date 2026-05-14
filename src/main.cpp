#include <Windows.h>

#include <sstream>
#include <cstdlib>
#include <string>

#include "constants.h"

import components.Config;
import components.I18n;
import components.Controller;
import components.NetworkBlocker;
import common.Common;
import common.Utils;
import profile.Manager;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // 加载配置文件和语言
    try {
        Config::instance().initialize();
        I18n::instance().initialize();

        // 检查是否已经运行了一个实例
        const MutexGuard mutexGuard(PROGRAM_MUTEX);
        if(mutexGuard.is_run()) {
            Controller controller(hInstance);
            Controller::run();
        } else
            exit(EXIT_FAILURE);
    } catch(std::exception& msg) {
        MessageBoxW(nullptr, utf8_to_wide(msg.what()).c_str(), L"error",MB_ICONINFORMATION);
        NetworkBlocker::instance().unblock_network();
        exit(EXIT_FAILURE);
    }

    NetworkBlocker::instance().unblock_network();
    exit(EXIT_SUCCESS);
}
