#include "BackgroundThread.hpp"
#include "ftp.h"
#include "net.h"
#include <sys/socket.h>

BackgroundThread *BackgroundThread::instance = nullptr;

BackgroundThread::BackgroundThread() : BackgroundThreadWrapper(BackgroundThread::getPriority()) {
    DEBUG_FUNCTION_LINE("Start FTP Server");
    std::lock_guard<std::recursive_mutex> lock(mutex);
    this->serverSocket = create_server(PORT);
    OSMemoryBarrier();
    DEBUG_FUNCTION_LINE_VERBOSE("Resume Thread");
    CThread::resumeThread();
}

BackgroundThread::~BackgroundThread() {
    DEBUG_FUNCTION_LINE("Shutting down FTP Server");
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (this->serverSocket >= 0) {
        cleanup_ftp();
        network_close(this->serverSocket);
        this->serverSocket = -1;
    }
}

BOOL BackgroundThread::whileLoop() {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (this->serverSocket >= 0) {
        network_down = process_ftp_events(this->serverSocket);
        if (network_down) {
            DEBUG_FUNCTION_LINE_VERBOSE("Network is down %d", this->serverSocket);
            cleanup_ftp();
            network_close(this->serverSocket);
            this->serverSocket = -1;
            DCFlushRange(&(this->serverSocket), 4);
        }
    } else {
        this->serverSocket = create_server(PORT);
        if (this->serverSocket < 0) {
            DEBUG_FUNCTION_LINE_WARN("Creating a new ftp server failed. Trying again in 5 seconds.");
            OSSleepTicks(OSSecondsToTicks(5));
        }
    }
    return true;
}
