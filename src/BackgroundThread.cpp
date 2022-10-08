#include "BackgroundThread.hpp"
#include "ftp.h"
#include "net.h"
#include <sys/socket.h>

BackgroundThread *BackgroundThread::instance = nullptr;

BackgroundThread::BackgroundThread() : BackgroundThreadWrapper(BackgroundThread::getPriority()) {
    DEBUG_FUNCTION_LINE("Start FTP Server");
    this->serverSocket = create_server(PORT);
    OSMemoryBarrier();
    DEBUG_FUNCTION_LINE_VERBOSE("Resume Thread");
    CThread::resumeThread();
}

BackgroundThread::~BackgroundThread() {
    DEBUG_FUNCTION_LINE("Shutting down FTP Server");
    stopThread();
    while (!hasThreadStopped()) {
        OSSleepTicks(OSMillisecondsToTicks(10));
    }
    if (this->serverSocket >= 0) {
        cleanup_ftp();
        network_close(this->serverSocket);
        this->serverSocket = -1;
    }
}

BOOL BackgroundThread::whileLoop() {
    if (this->serverSocket >= 0) {
        network_down = process_ftp_events(this->serverSocket);
        if (network_down) {
            DEBUG_FUNCTION_LINE_WARN("Network is down");
            cleanup_ftp();
            network_close(this->serverSocket);
            this->serverSocket = -1;
            OSMemoryBarrier();
        }
    } else {
        this->serverSocket = create_server(PORT);
        if (this->serverSocket < 0) {
            if (errno != EBUSY) {
                DEBUG_FUNCTION_LINE_WARN("Creating server failed: %d", errno);
            }
            OSSleepTicks(OSMillisecondsToTicks(10));
        }
    }
    return true;
}
