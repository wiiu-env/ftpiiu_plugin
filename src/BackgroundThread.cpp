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
    CThread::resumeThread();
}

BackgroundThread::~BackgroundThread() {
    DEBUG_FUNCTION_LINE("Shutting down FTP Server");
    if (this->serverSocket != -1) {
        mutex.lock();
        cleanup_ftp();
        network_close(this->serverSocket);
        mutex.unlock();
        this->serverSocket = -1;
    }
}

BOOL BackgroundThread::whileLoop() {
    if (this->serverSocket != -1) {
        mutex.lock();
        network_down = process_ftp_events(this->serverSocket);
        mutex.unlock();
        if (network_down) {
            DEBUG_FUNCTION_LINE("Network is down %d", this->serverSocket);
            mutex.lock();
            cleanup_ftp();
            network_close(this->serverSocket);
            this->serverSocket = -1;
            DCFlushRange(&(this->serverSocket), 4);
            mutex.unlock();
        }
    }
    OSSleepTicks(OSMillisecondsToTicks(16));
    return true;
}
