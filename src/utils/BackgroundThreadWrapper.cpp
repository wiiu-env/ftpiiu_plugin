#include "BackgroundThreadWrapper.hpp"
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <coreinit/cache.h>

#include "logger.h"

BackgroundThreadWrapper::BackgroundThreadWrapper(int32_t priority): CThread(CThread::eAttributeAffCore2, priority, 0x100000) {
}

BackgroundThreadWrapper::~BackgroundThreadWrapper() {
    exitThread = 1;
    DCFlushRange((void*)&exitThread, 4);
    DEBUG_FUNCTION_LINE("Exit thread\n");
}

void BackgroundThreadWrapper::executeThread() {
    while (1) {
        if(exitThread) {
            DEBUG_FUNCTION_LINE("We want to exit\n");
            break;
        }
        if(!whileLoop()){
            break;
        }
    }
    DEBUG_FUNCTION_LINE("Exit!\n");
}

