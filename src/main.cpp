#include "BackgroundThread.hpp"
#include "utils/logger.h"
#include "virtualpath.h"
#include <coreinit/cache.h>
#include <cstring>
#include <mocha/mocha.h>
#include <nn/ac.h>
#include <wups.h>

WUPS_PLUGIN_NAME("FTPiiU");
WUPS_PLUGIN_DESCRIPTION("FTP Server");
WUPS_PLUGIN_VERSION("0.1");
WUPS_PLUGIN_AUTHOR("Maschell");
WUPS_PLUGIN_LICENSE("GPL");

WUPS_USE_WUT_DEVOPTAB();

uint32_t hostIpAddress = 0;

BackgroundThread *thread = nullptr;

MochaUtilsStatus MountWrapper(const char *mount, const char *dev, const char *mountTo) {
    auto res = Mocha_MountFS(mount, dev, mountTo);
    if (res == MOCHA_RESULT_ALREADY_EXISTS) {
        res = Mocha_MountFS(mount, nullptr, mountTo);
    }
    if (res == MOCHA_RESULT_SUCCESS) {
        std::string mountPath = std::string(mount) + ":/";
        VirtualMountDevice(mountPath.c_str());
        DEBUG_FUNCTION_LINE_VERBOSE("Mounted %s", mountPath.c_str());
    } else {
        DEBUG_FUNCTION_LINE_ERR("Failed to mount %s: %s [%d]", mount, Mocha_GetStatusStr(res), res);
    }
    return res;
}

/* Entry point */
ON_APPLICATION_START() {
    nn::ac::Initialize();
    nn::ac::ConnectAsync();
    nn::ac::GetAssignedAddress(&hostIpAddress);
    initLogging();

    //!*******************************************************************
    //!                        Initialize FS                             *
    //!*******************************************************************

    VirtualMountDevice("fs:/");
    AddVirtualFSPath("vol", nullptr, nullptr);
    AddVirtualFSVOLPath("external01", nullptr, nullptr);
    AddVirtualFSVOLPath("content", nullptr, nullptr);
    MochaUtilsStatus res;
    if ((res = Mocha_InitLibrary()) == MOCHA_RESULT_SUCCESS) {
        MountWrapper("slccmpt01", "/dev/slccmpt01", "/vol/storage_slccmpt01");
        MountWrapper("storage_odd_tickets", nullptr, "/vol/storage_odd01");
        MountWrapper("storage_odd_updates", nullptr, "/vol/storage_odd02");
        MountWrapper("storage_odd_content", nullptr, "/vol/storage_odd03");
        MountWrapper("storage_odd_content2", nullptr, "/vol/storage_odd04");
        MountWrapper("storage_slc", "/dev/slc01", "/vol/storage_slc01");
        Mocha_MountFS("storage_mlc", nullptr, "/vol/storage_mlc01");
        Mocha_MountFS("storage_usb", nullptr, "/vol/storage_usb01");
    } else {
        DEBUG_FUNCTION_LINE_ERR("Failed to init libmocha: %s [%d]", Mocha_GetStatusStr(res), res);
    }

    thread = BackgroundThread::getInstance();
    DCFlushRange(&thread, 4);
}

void stopThread() {
    BackgroundThread::destroyInstance();
}

ON_APPLICATION_REQUESTS_EXIT() {
    DEBUG_FUNCTION_LINE_VERBOSE("Ending ftp server");
    stopThread();

    DEBUG_FUNCTION_LINE_VERBOSE("Ended ftp Server.");

    Mocha_UnmountFS("slccmpt01");
    Mocha_UnmountFS("storage_odd_tickets");
    Mocha_UnmountFS("storage_odd_updates");
    Mocha_UnmountFS("storage_odd_content");
    Mocha_UnmountFS("storage_odd_content2");
    Mocha_UnmountFS("storage_slc");
    Mocha_UnmountFS("storage_mlc");
    Mocha_UnmountFS("storage_usb");

    DEBUG_FUNCTION_LINE("Unmount virtual paths");
    UnmountVirtualPaths();

    deinitLogging();
}
