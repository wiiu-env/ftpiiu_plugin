#pragma once

#include "utils/BackgroundThreadWrapper.hpp"
#include "utils/logger.h"
#include <coreinit/cache.h>

#define PORT 21

class BackgroundThread : BackgroundThreadWrapper {
public:
    static BackgroundThread *getInstance() {
        if (instance == nullptr) {
            instance = new BackgroundThread();
            OSMemoryBarrier();
        }
        return instance;
    }

    static void destroyInstance() {
        if (instance != nullptr) {
            delete instance;
            instance = nullptr;
            OSMemoryBarrier();
        }
    }

    BackgroundThread();

    ~BackgroundThread() override;

private:
    static int32_t getPriority() {
        return 16;
    }

    BOOL whileLoop() override;

    static BackgroundThread *instance;

    int serverSocket = -1;
    int network_down = 0;
};
