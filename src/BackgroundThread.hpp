#pragma once

#include "utils/BackgroundThreadWrapper.hpp"
#include <coreinit/cache.h>
#include "utils/logger.h"

#define PORT                    21

class BackgroundThread : BackgroundThreadWrapper {
public:
    static BackgroundThread *getInstance() {
        DCFlushRange(&instance, sizeof(BackgroundThread));
        ICInvalidateRange(&instance, sizeof(BackgroundThread));
        if (instance == nullptr) {
            instance = new BackgroundThread();
            DCFlushRange(&instance, sizeof(BackgroundThread));
            ICInvalidateRange(&instance, sizeof(BackgroundThread));
        }
        return instance;
    }

    static void destroyInstance() {
        DCFlushRange(&instance, sizeof(BackgroundThread));
        ICInvalidateRange(&instance, sizeof(BackgroundThread));
        DEBUG_FUNCTION_LINE("Instance is %08X\n", instance);
        OSSleepTicks(OSSecondsToTicks(1));
        if (instance != nullptr) {
            delete instance;
            instance = nullptr;
            DCFlushRange(&instance, sizeof(BackgroundThread));
            ICInvalidateRange(&instance, sizeof(BackgroundThread));
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
