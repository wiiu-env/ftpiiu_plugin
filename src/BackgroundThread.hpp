#pragma once

#include "utils/BackgroundThreadWrapper.hpp"
#include <coreinit/cache.h>
#include "utils/logger.h"

#define PORT                    21

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
