#pragma once

#include "CThread.h"
#include <mutex>
#include <wut_types.h>

class BackgroundThreadWrapper : public CThread {
public:
    explicit BackgroundThreadWrapper(int32_t priority);

    ~BackgroundThreadWrapper() override;

protected:
    [[nodiscard]] BOOL shouldExit() const {
        return (exitThread);
    }

    void setThreadPriority(int32_t priority) override {
        CThread::setThreadPriority(priority);
    }

    void stopThread() {
        exitThread = true;
    }

    bool hasThreadStopped() {
        return threadEnded;
    }

private:
    volatile bool threadEnded = false;
    volatile bool exitThread  = false;

    void executeThread() override;

    virtual BOOL whileLoop() = 0;
};
