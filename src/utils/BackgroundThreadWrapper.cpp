#include "BackgroundThreadWrapper.hpp"

#include <string.h>
#include <coreinit/cache.h>


BackgroundThreadWrapper::BackgroundThreadWrapper(int32_t priority): CThread(CThread::eAttributeAffCore2, priority, 0x100000) {
}

BackgroundThreadWrapper::~BackgroundThreadWrapper() {
    exitThread = 1;
    DCFlushRange((void*)&exitThread, 4);
    DEBUG_FUNCTION_LINE("Exit thread");
}

void BackgroundThreadWrapper::executeThread() {
    while (true) {
        if(exitThread) {
            DEBUG_FUNCTION_LINE("We want to exit");
            break;
        }
        if(!whileLoop()){
            break;
        }
    }
    DEBUG_FUNCTION_LINE("Exit!");
}

