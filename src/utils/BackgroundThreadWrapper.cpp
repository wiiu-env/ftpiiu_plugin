#include "BackgroundThreadWrapper.hpp"
#include <coreinit/cache.h>

BackgroundThreadWrapper::BackgroundThreadWrapper(int32_t priority) : CThread(CThread::eAttributeAffCore2, priority, 0x100000, nullptr, nullptr, "FTPiiU Server") {
}

BackgroundThreadWrapper::~BackgroundThreadWrapper() {
    exitThread = 1;
    stopThread();
    OSMemoryBarrier();
}

void BackgroundThreadWrapper::executeThread() {
    while (true) {
        if (exitThread) {
            break;
        }
        if (!whileLoop()) {
            break;
        }
    }
    threadEnded = true;
    OSMemoryBarrier();
}
