#include "BackgroundThread.hpp"
#include <cstring>
#include "ftp.h"
#include "net.h"

BackgroundThread *BackgroundThread::instance = nullptr;

BackgroundThread::BackgroundThread() : BackgroundThreadWrapper(BackgroundThread::getPriority()) {
    DEBUG_FUNCTION_LINE("Start FTP Server");
    mutex.lock();
    this->serverSocket = create_server(PORT);
    DCFlushRange(&(this->serverSocket), 4);
    mutex.unlock();
    DEBUG_FUNCTION_LINE("Resume Thread");
    CThread::resumeThread();
}

BackgroundThread::~BackgroundThread() {
    DEBUG_FUNCTION_LINE("Shutting down FTP Server");
    mutex.lock();
    if (this->serverSocket >= 0) {
        cleanup_ftp();
        network_close(this->serverSocket);
        this->serverSocket = -1;
    }
    mutex.unlock();
}

BOOL BackgroundThread::whileLoop() {
    mutex.lock();
    if (this->serverSocket >= 0) {
        network_down = process_ftp_events(this->serverSocket);
        if (network_down) {
            DEBUG_FUNCTION_LINE("Network is down %d", this->serverSocket);
            cleanup_ftp();
            network_close(this->serverSocket);
            this->serverSocket = -1;
            DCFlushRange(&(this->serverSocket), 4);
        }
    }
    mutex.unlock();
    OSSleepTicks(OSMillisecondsToTicks(16));
    return true;
}
