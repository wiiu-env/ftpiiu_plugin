#include "main.h"
#include "BackgroundThread.hpp"
#include "utils/logger.h"
#include "virtualpath.h"
#include <coreinit/cache.h>
#include <cstring>
#include <mocha/mocha.h>
#include <nn/ac.h>
#include <stdio.h>
#include <wups.h>
#include <wups/config/WUPSConfigItemBoolean.h>
#include <wups/config/WUPSConfigItemStub.h>

WUPS_PLUGIN_NAME("FTPiiU");
WUPS_PLUGIN_DESCRIPTION("FTP Server");
WUPS_PLUGIN_VERSION(VERSION_FULL);
WUPS_PLUGIN_AUTHOR("Maschell");
WUPS_PLUGIN_LICENSE("GPL");

WUPS_USE_WUT_DEVOPTAB();
WUPS_USE_STORAGE("ftpiiu"); // Unqiue id for the storage api

uint32_t hostIpAddress = 0;

BackgroundThread *thread = nullptr;

#define FTPIIU_ENABLED_STRING       "enabled"
#define SYSTEM_FILES_ALLOWED_STRING "systemFilesAllowed"

bool gFTPServerEnabled   = true;
bool gSystemFilesAllowed = false;

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
void startServer();
void stopServer();

/* Entry point */
ON_APPLICATION_START() {
    nn::ac::Initialize();
    nn::ac::ConnectAsync();
    hostIpAddress = 0;
    nn::ac::GetAssignedAddress(&hostIpAddress);
    initLogging();

    //Make sure the server instance is destroyed.
    BackgroundThread::destroyInstance();
    if (gFTPServerEnabled) {
        startServer();
    }
}

INITIALIZE_PLUGIN() {
    // Open storage to read values
    WUPSStorageError storageRes = WUPS_OpenStorage();
    if (storageRes != WUPS_STORAGE_ERROR_SUCCESS) {
        DEBUG_FUNCTION_LINE_ERR("Failed to open storage %s (%d)", WUPS_GetStorageStatusStr(storageRes), storageRes);
    } else {
        if ((storageRes = WUPS_GetBool(nullptr, FTPIIU_ENABLED_STRING, &gFTPServerEnabled)) == WUPS_STORAGE_ERROR_NOT_FOUND) {
            // Add the value to the storage if it's missing.
            if (WUPS_StoreBool(nullptr, FTPIIU_ENABLED_STRING, gFTPServerEnabled) != WUPS_STORAGE_ERROR_SUCCESS) {
                DEBUG_FUNCTION_LINE_ERR("Failed to store bool");
            }
        } else if (storageRes != WUPS_STORAGE_ERROR_SUCCESS) {
            DEBUG_FUNCTION_LINE_ERR("Failed to get bool %s (%d)", WUPS_GetStorageStatusStr(storageRes), storageRes);
        }
        if ((storageRes = WUPS_GetBool(nullptr, SYSTEM_FILES_ALLOWED_STRING, &gSystemFilesAllowed)) == WUPS_STORAGE_ERROR_NOT_FOUND) {
            // Add the value to the storage if it's missing.
            if (WUPS_StoreBool(nullptr, SYSTEM_FILES_ALLOWED_STRING, gSystemFilesAllowed) != WUPS_STORAGE_ERROR_SUCCESS) {
                DEBUG_FUNCTION_LINE_ERR("Failed to store bool");
            }
        } else if (storageRes != WUPS_STORAGE_ERROR_SUCCESS) {
            DEBUG_FUNCTION_LINE_ERR("Failed to get bool %s (%d)", WUPS_GetStorageStatusStr(storageRes), storageRes);
        }

        // Close storage
        if (WUPS_CloseStorage() != WUPS_STORAGE_ERROR_SUCCESS) {
            DEBUG_FUNCTION_LINE_ERR("Failed to close storage");
        }
    }
    thread = nullptr;
}

void startServer() {
    if (!thread) {
        //!*******************************************************************
        //!                        Initialize FS                             *
        //!*******************************************************************
        if (gSystemFilesAllowed) {
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
        }

        MountVirtualDevices();

        thread = BackgroundThread::getInstance();
        OSMemoryBarrier();
    }
}


void stopServer() {
    BackgroundThread::destroyInstance();
    if (gSystemFilesAllowed) {
        Mocha_UnmountFS("slccmpt01");
        Mocha_UnmountFS("storage_odd_tickets");
        Mocha_UnmountFS("storage_odd_updates");
        Mocha_UnmountFS("storage_odd_content");
        Mocha_UnmountFS("storage_odd_content2");
        Mocha_UnmountFS("storage_slc");
        Mocha_UnmountFS("storage_mlc");
        Mocha_UnmountFS("storage_usb");
    }

    DEBUG_FUNCTION_LINE("Unmount virtual paths");
    UnmountVirtualPaths();

    thread = nullptr;
}

void gFTPServerRunningChanged(ConfigItemBoolean *item, bool newValue) {
    DEBUG_FUNCTION_LINE("New value in gFTPServerEnabled: %d", newValue);
    gFTPServerEnabled = newValue;
    if (!gFTPServerEnabled) {
        stopServer();
    } else {
        startServer();
    }
    // If the value has changed, we store it in the storage.
    auto res = WUPS_StoreInt(nullptr, FTPIIU_ENABLED_STRING, gFTPServerEnabled);
    if (res != WUPS_STORAGE_ERROR_SUCCESS) {
        DEBUG_FUNCTION_LINE_ERR("Failed to store gFTPServerEnabled: %s (%d)", WUPS_GetStorageStatusStr(res), res);
    }
}

void gSystemFilesAllowedChanged(ConfigItemBoolean *item, bool newValue) {
    DEBUG_FUNCTION_LINE("New value in gFTPServerEnabled: %d", newValue);
    if (thread != nullptr) { // If the server is already running we need to restart it.
        stopServer();
        gSystemFilesAllowed = newValue;
        startServer();
    } else {
        gSystemFilesAllowed = newValue;
    }
    // If the value has changed, we store it in the storage.
    auto res = WUPS_StoreInt(nullptr, SYSTEM_FILES_ALLOWED_STRING, gSystemFilesAllowed);
    if (res != WUPS_STORAGE_ERROR_SUCCESS) {
        DEBUG_FUNCTION_LINE_ERR("Failed to store gSystemFilesAllowed: %s (%d)", WUPS_GetStorageStatusStr(res), res);
    }
}

WUPS_GET_CONFIG() {
    // We open the storage, so we can persist the configuration the user did.
    if (WUPS_OpenStorage() != WUPS_STORAGE_ERROR_SUCCESS) {
        DEBUG_FUNCTION_LINE_ERR("Failed to open storage");
        return 0;
    }
    nn::ac::GetAssignedAddress(&hostIpAddress);

    WUPSConfigHandle config;
    WUPSConfig_CreateHandled(&config, "FTPiiU");

    WUPSConfigCategoryHandle setting;
    WUPSConfig_AddCategoryByNameHandled(config, "Settings", &setting);

    WUPSConfigItemBoolean_AddToCategoryHandled(config, setting, FTPIIU_ENABLED_STRING, "Enable FTPiiU", gFTPServerEnabled, &gFTPServerRunningChanged);
    WUPSConfigItemBoolean_AddToCategoryHandled(config, setting, SYSTEM_FILES_ALLOWED_STRING, "Allow access to system files", gSystemFilesAllowed, &gSystemFilesAllowedChanged);

    WUPSConfigCategoryHandle info;
    WUPSConfig_AddCategoryByNameHandled(config, "==========", &info);
    WUPSConfigItemStub_AddToCategoryHandled(config, info, "info", "Press B to go Back");

    WUPSConfigCategoryHandle info1;
    char ipSettings[50];
    if (hostIpAddress != 0) {
        snprintf(ipSettings, 50, "IP of your console is %u.%u.%u.%u. Port %i",
                 (hostIpAddress >> 24) & 0xFF,
                 (hostIpAddress >> 16) & 0xFF,
                 (hostIpAddress >> 8) & 0xFF,
                 (hostIpAddress >> 0) & 0xFF,
                 PORT);
    } else {
        snprintf(ipSettings, 50, "The console is not connected to a network.");
    }
    WUPSConfig_AddCategoryByNameHandled(config, ipSettings, &info1);
    WUPSConfigItemStub_AddToCategoryHandled(config, info1, "info1", "Press B to go Back");

    WUPSConfigCategoryHandle info2;
    char portSettings[50];
    snprintf(portSettings, 50, "You can connect with empty credentials");
    WUPSConfig_AddCategoryByNameHandled(config, portSettings, &info2);
    WUPSConfigItemStub_AddToCategoryHandled(config, info2, "info2", "Press B to go Back");

    return config;
}

WUPS_CONFIG_CLOSED() {
    // Save all changes
    if (WUPS_CloseStorage() != WUPS_STORAGE_ERROR_SUCCESS) {
        DEBUG_FUNCTION_LINE_ERR("Failed to close storage");
    }
}

ON_APPLICATION_REQUESTS_EXIT() {
    stopServer();
    deinitLogging();
}
