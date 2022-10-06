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
            DCFlushRange(&instance, 4);
        }
        return instance;
    }

    static void destroyInstance() {
        if (instance != nullptr) {
            delete instance;
            instance = nullptr;
            DCFlushRange(&instance, 4);
        }
    }
    static void destroyInstance(bool forceKill) {
        if (instance != nullptr) {
            instance->skipJoin = true;
        }
        destroyInstance();
    }

    BackgroundThread();

    ~BackgroundThread() override;

private:
    static int32_t getPriority() {
        return 17;
    }

    BOOL whileLoop() override;

    static BackgroundThread *instance;

    int serverSocket = -1;
    int network_down = 0;
};
