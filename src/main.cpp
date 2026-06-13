#include <Windows.h>

#include <cstdlib>
#include <sstream>
#include <string>

#include "constants.h"

import components.Config;
import components.I18n;
import components.Service;
import components.NetworkBlocker;
import common.Common;
import common.Utils;
import profile.Manager;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    ScopeGuard unblock_on_exit([] {
        NetworkBlocker::instance().unblock_network();
    });

    // 加载配置文件和语言
    try {
        Config config;
        config.load(CONFIG_FILE);
        {
            I18n::instance().initialize(config.lang);
            NetworkBlocker::initialize(config.block_network);
            ProfileManager::initialize(config.profiles);
        }
        // 检查是否已经运行了一个实例
        const MutexGuard mutexGuard(PROGRAM_MUTEX);
        if(mutexGuard.acquired()) {
            Service service(hInstance, config);
            Service::run();
        } else
            exit(EXIT_FAILURE);
    } catch(std::exception& msg) {
        MessageBoxW(nullptr, utf8_to_wide(msg.what()).c_str(), L"error", MB_ICONINFORMATION);
        exit(EXIT_FAILURE);
    }
    exit(EXIT_SUCCESS);
}
