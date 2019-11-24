#pragma once
#include "utils/BackgroundThreadWrapper.hpp"
#include <coreinit/cache.h>
#include "utils/logger.h"

#define PORT                    21

class BackgroundThread: BackgroundThreadWrapper {
public:
    static BackgroundThread *getInstance() {
                    DCFlushRange(&instance, sizeof(instance));
            ICInvalidateRange(&instance, sizeof(instance));
        if(instance == NULL) {
            instance = new BackgroundThread();
            DCFlushRange(&instance, sizeof(instance));
            ICInvalidateRange(&instance, sizeof(instance));
        }
        return instance;
    }

    static void destroyInstance() {
        DCFlushRange(&instance, sizeof(instance));
        ICInvalidateRange(&instance, sizeof(instance));
        DEBUG_FUNCTION_LINE("Instance is %08X\n", instance);
        OSSleepTicks(OSSecondsToTicks(1));
        if(instance != NULL) {
            delete instance;
            instance = NULL;
            DCFlushRange(&instance, sizeof(instance));
            ICInvalidateRange(&instance, sizeof(instance));
        }
    }

    BackgroundThread();

    virtual ~BackgroundThread();

private:
    static int32_t getPriority() {
        return 16;
    }

    virtual BOOL whileLoop();

    static BackgroundThread * instance;

    int serverSocket = -1;
    int network_down = 0;

};
