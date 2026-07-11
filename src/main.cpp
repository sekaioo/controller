#include <Windows.h>

#include <cstdlib>
#include <sstream>
#include <string>

#include "constants.h"

import components.Config;
import components.I18n;
import components.Service;
import components.NetworkBlocker;
import components.Logger;
import common.Common;
import common.Utils;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    ScopeGuard unblock_on_exit([] {
        network_blocker::unblock();
    });

    // 加载配置文件和语言
    try {
        Config config;
        config.load(exe_relative_path(CONFIG_FILE));
        {
            Logger::initialize(config.log.disabled, config.log.level, config.log.output, config.log.timestamp);
            I18n::instance().initialize(config.lang);
            network_blocker::initialize(config.block_network);
        }
        // 检查是否已经运行了一个实例
        const MutexGuard mutexGuard(PROGRAM_MUTEX);
        if(mutexGuard.acquired()) {
            Service service(hInstance, config);
            service.run();
        } else
            return EXIT_FAILURE;
    } catch(std::exception& msg) {
        Logger::log(msg.what(), Logger::FATAL);
        MessageBoxW(nullptr, utf8_to_wide(msg.what()).c_str(), L"error", MB_ICONINFORMATION);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
