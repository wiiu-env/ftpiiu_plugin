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
        return (exitThread == 1);
    }

    void setThreadPriority(int32_t priority) override {
        CThread::setThreadPriority(priority);
    }

    std::recursive_mutex mutex;

private:
    void executeThread() override;

    virtual BOOL whileLoop() = 0;

    volatile int32_t exitThread = 0;
};
