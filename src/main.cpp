
#include <wups.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <malloc.h>
#include <stdint.h>
#include <iosuhax.h>
#include <iosuhax_devoptab.h>
#include <iosuhax_disc_interface.h>
#include <proc_ui/procui.h>
#include <coreinit/foreground.h>

#include <coreinit/screen.h>
#include <sysapp/launch.h>
#include <coreinit/dynload.h>
#include <nn/ac.h>
#include <stdarg.h>
#include <whb/proc.h>
#include <stdlib.h>
#include <coreinit/thread.h>
#include <coreinit/cache.h>
#include <coreinit/time.h>
#include <stdio.h>
#include <whb/libmanager.h>
#include "utils/logger.h"
#include "utils/utils.h"
#include <whb/log_udp.h>

#include "virtualpath.h"
#include "net.h"
#include "BackgroundThread.hpp"

#define MAX_CONSOLE_LINES_TV    27
#define MAX_CONSOLE_LINES_DRC   18

WUPS_PLUGIN_NAME("FTPiiU");
WUPS_PLUGIN_DESCRIPTION("FTP Server");
WUPS_PLUGIN_VERSION("0.1");
WUPS_PLUGIN_AUTHOR("Maschell");
WUPS_PLUGIN_LICENSE("GPL");

WUPS_USE_WUT_CRT()

uint32_t hostIpAddress = 0;
int iosuhaxMount = 0;
int fsaFd = 0;

BackgroundThread * thread = NULL;

/* Entry point */
ON_APPLICATION_START(args) {
    WHBInitializeSocketLibrary();

    nn::ac::ConfigIdNum configId;

    nn::ac::Initialize();
    nn::ac::GetStartupId(&configId);
    nn::ac::Connect(configId);

    ACGetAssignedAddress(&hostIpAddress);

    WHBLogUdpInit();

    //!*******************************************************************
    //!                        Initialize FS                             *
    //!*******************************************************************

    int fsaFd = -1;

    DEBUG_FUNCTION_LINE("IOSUHAX_Open");
    int res = IOSUHAX_Open(NULL);
    if(res < 0) {
        DEBUG_FUNCTION_LINE("IOSUHAX_open failed");
        VirtualMountDevice("fs:/");
    } else {
        iosuhaxMount = 1;
        //fatInitDefault();

        DEBUG_FUNCTION_LINE("IOSUHAX_FSA_Open");
        fsaFd = IOSUHAX_FSA_Open();
        if(fsaFd < 0) {
            DEBUG_FUNCTION_LINE("IOSUHAX_FSA_Open failed");
        }

        DEBUG_FUNCTION_LINE("IOSUHAX_FSA_Open");

        mount_fs("slccmpt01", fsaFd, "/dev/slccmpt01", "/vol/storage_slccmpt01");
        mount_fs("storage_odd_tickets", fsaFd, "/dev/odd01", "/vol/storage_odd_tickets");
        mount_fs("storage_odd_updates", fsaFd, "/dev/odd02", "/vol/storage_odd_updates");
        mount_fs("storage_odd_content", fsaFd, "/dev/odd03", "/vol/storage_odd_content");
        mount_fs("storage_odd_content2", fsaFd, "/dev/odd04", "/vol/storage_odd_content2");
        mount_fs("storage_slc", fsaFd, NULL, "/vol/system");
        mount_fs("storage_mlc", fsaFd, NULL, "/vol/storage_mlc01");
        mount_fs("storage_usb", fsaFd, NULL, "/vol/storage_usb01");

        VirtualMountDevice("fs:/");
        VirtualMountDevice("slccmpt01:/");
        VirtualMountDevice("storage_odd_tickets:/");
        VirtualMountDevice("storage_odd_updates:/");
        VirtualMountDevice("storage_odd_content:/");
        VirtualMountDevice("storage_odd_content2:/");
        VirtualMountDevice("storage_slc:/");
        VirtualMountDevice("storage_mlc:/");
        VirtualMountDevice("storage_usb:/");
        VirtualMountDevice("usb:/");
    }

    thread = BackgroundThread::getInstance();
    DCFlushRange(&thread, 4);
}

void stopThread(){
    BackgroundThread::destroyInstance();
}

ON_APPLICATION_END(){
    DEBUG_FUNCTION_LINE("Ending ftp server");
    stopThread();

    if(iosuhaxMount) {
        IOSUHAX_sdio_disc_interface.shutdown();
        IOSUHAX_usb_disc_interface.shutdown();

        unmount_fs("slccmpt01");
        unmount_fs("storage_odd_tickets");
        unmount_fs("storage_odd_updates");
        unmount_fs("storage_odd_content");
        unmount_fs("storage_odd_content2");
        unmount_fs("storage_slc");
        unmount_fs("storage_mlc");
        unmount_fs("storage_usb");
        IOSUHAX_FSA_Close(fsaFd);
        IOSUHAX_Close();
    }

    UnmountVirtualPaths();
}

