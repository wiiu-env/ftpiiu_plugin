/****************************************************************************
* Copyright (C) 2008
* Joseph Jordan <joe.ftpii@psychlaw.com.au>
*
* Copyright (C) 2010
* by Dimok
*
* This software is provided 'as-is', without any express or implied
* warranty. In no event will the authors be held liable for any
* damages arising from the use of this software.
*
* Permission is granted to anyone to use this software for any
* purpose, including commercial applications, and to alter it and
* redistribute it freely, subject to the following restrictions:
*
* 1. The origin of this software must not be misrepresented; you
* must not claim that you wrote the original software. If you use
* this software in a product, an acknowledgment in the product
* documentation would be appreciated but is not required.
*
* 2. Altered source versions must be plainly marked as such, and
* must not be misrepresented as being the original software.
*
* 3. This notice may not be removed or altered from any source
* distribution.
*
* for WiiXplorer 2010
***************************************************************************/
#include "virtualpath.h"
#include "main.h"
#include "utils/logger.h"
#include <malloc.h>
#include <string.h>

uint8_t MAX_VIRTUAL_PARTITIONS        = 0;
VIRTUAL_PARTITION *VIRTUAL_PARTITIONS = NULL;

uint8_t MAX_VIRTUAL_FS        = 0;
VIRTUAL_PARTITION *VIRTUAL_FS = NULL;

uint8_t MAX_VIRTUAL_FS_VOL        = 0;
VIRTUAL_PARTITION *VIRTUAL_FS_VOL = NULL;

void VirtualMountDevice(const char *path) {
    if (!path)
        return;

    int i = 0;
    char name[255];
    char alias[255];
    char prefix[255];
    bool namestop = false;

    alias[0] = '/';

    do {
        if (path[i] == ':')
            namestop = true;

        if (!namestop) {
            name[i]      = path[i];
            name[i + 1]  = '\0';
            alias[i + 1] = path[i];
            alias[i + 2] = '\0';
        }

        prefix[i]     = path[i];
        prefix[i + 1] = '\0';
        i++;
    } while (path[i - 1] != '/');
    AddVirtualPath(name, alias, prefix);
}

void AddVirtualPath(const char *name, const char *alias, const char *prefix) {
    if (!VIRTUAL_PARTITIONS) {
        VIRTUAL_PARTITIONS = (VIRTUAL_PARTITION *) malloc(sizeof(VIRTUAL_PARTITION));
        if (!VIRTUAL_PARTITIONS) {
            DEBUG_FUNCTION_LINE_ERR("Failed to allocate VIRTUAL_PARTITIONS");
            return;
        }
    }

    VIRTUAL_PARTITION *tmp = realloc(VIRTUAL_PARTITIONS, sizeof(VIRTUAL_PARTITION) * (MAX_VIRTUAL_PARTITIONS + 1));
    if (!tmp) {
        DEBUG_FUNCTION_LINE_ERR("Failed to reallocate VIRTUAL_PARTITIONS");
        if (VIRTUAL_PARTITIONS) {
            for (int i = 0; i < MAX_VIRTUAL_PARTITIONS; i++) {
                if (VIRTUAL_PARTITIONS[i].name) {
                    free(VIRTUAL_PARTITIONS[i].name);
                    VIRTUAL_PARTITIONS[i].name = NULL;
                }
                if (VIRTUAL_PARTITIONS[i].alias) {
                    free(VIRTUAL_PARTITIONS[i].alias);
                    VIRTUAL_PARTITIONS[i].alias = NULL;
                }
                if (VIRTUAL_PARTITIONS[i].prefix) {
                    free(VIRTUAL_PARTITIONS[i].prefix);
                    VIRTUAL_PARTITIONS[i].prefix = NULL;
                }
            }
            free(VIRTUAL_PARTITIONS);
        }
        VIRTUAL_PARTITIONS     = NULL;
        MAX_VIRTUAL_PARTITIONS = 0;
        return;
    }

    VIRTUAL_PARTITIONS = tmp;

    VIRTUAL_PARTITIONS[MAX_VIRTUAL_PARTITIONS].name     = strdup(name);
    VIRTUAL_PARTITIONS[MAX_VIRTUAL_PARTITIONS].alias    = strdup(alias);
    VIRTUAL_PARTITIONS[MAX_VIRTUAL_PARTITIONS].prefix   = strdup(prefix);
    VIRTUAL_PARTITIONS[MAX_VIRTUAL_PARTITIONS].inserted = true;

    MAX_VIRTUAL_PARTITIONS++;
}

void AddVirtualFSPath(const char *name, const char *alias, const char *prefix) {
    if (!VIRTUAL_FS) {
        VIRTUAL_FS = (VIRTUAL_PARTITION *) malloc(sizeof(VIRTUAL_PARTITION));
        if (!VIRTUAL_FS) {
            DEBUG_FUNCTION_LINE_ERR("Failed to allocate VIRTUAL_FS");
            return;
        }
    }

    VIRTUAL_PARTITION *tmp = realloc(VIRTUAL_FS, sizeof(VIRTUAL_PARTITION) * (MAX_VIRTUAL_FS + 1));
    if (!tmp) {
        DEBUG_FUNCTION_LINE_ERR("Failed to reallocate VIRTUAL_FS");
        if (VIRTUAL_FS) {
            for (int i = 0; i < MAX_VIRTUAL_FS; i++) {
                if (VIRTUAL_FS[i].name) {
                    free(VIRTUAL_FS[i].name);
                    VIRTUAL_FS[i].name = NULL;
                }
                if (VIRTUAL_FS[i].alias) {
                    free(VIRTUAL_FS[i].alias);
                    VIRTUAL_FS[i].alias = NULL;
                }
                if (VIRTUAL_FS[i].prefix) {
                    free(VIRTUAL_FS[i].prefix);
                    VIRTUAL_FS[i].prefix = NULL;
                }
            }
            free(VIRTUAL_FS);
        }
        VIRTUAL_FS     = NULL;
        MAX_VIRTUAL_FS = 0;
        return;
    }

    VIRTUAL_FS = tmp;

    VIRTUAL_FS[MAX_VIRTUAL_FS].name     = strdup(name);
    VIRTUAL_FS[MAX_VIRTUAL_FS].alias    = NULL;
    VIRTUAL_FS[MAX_VIRTUAL_FS].prefix   = NULL;
    VIRTUAL_FS[MAX_VIRTUAL_FS].inserted = true;

    MAX_VIRTUAL_FS++;
}

void AddVirtualFSVOLPath(const char *name, const char *alias, const char *prefix) {
    if (!VIRTUAL_FS_VOL) {
        VIRTUAL_FS_VOL = (VIRTUAL_PARTITION *) malloc(sizeof(VIRTUAL_PARTITION));
        if (!VIRTUAL_FS_VOL) {
            DEBUG_FUNCTION_LINE_ERR("Failed to allocate VIRTUAL_FS_VOL");
            return;
        }
    }

    VIRTUAL_PARTITION *tmp = realloc(VIRTUAL_FS_VOL, sizeof(VIRTUAL_PARTITION) * (MAX_VIRTUAL_FS_VOL + 1));
    if (!tmp) {
        DEBUG_FUNCTION_LINE_ERR("Failed to reallocate VIRTUAL_FS_VOL");
        if (VIRTUAL_FS_VOL) {
            for (int i = 0; i < MAX_VIRTUAL_FS_VOL; i++) {
                if (VIRTUAL_FS_VOL[i].name) {
                    free(VIRTUAL_FS_VOL[i].name);
                    VIRTUAL_FS_VOL[i].name = NULL;
                }
                if (VIRTUAL_FS_VOL[i].alias) {
                    free(VIRTUAL_FS_VOL[i].alias);
                    VIRTUAL_FS_VOL[i].alias = NULL;
                }
                if (VIRTUAL_FS_VOL[i].prefix) {
                    free(VIRTUAL_FS_VOL[i].prefix);
                    VIRTUAL_FS_VOL[i].prefix = NULL;
                }
            }
            free(VIRTUAL_FS_VOL);
        }
        VIRTUAL_FS_VOL     = NULL;
        MAX_VIRTUAL_FS_VOL = 0;
        return;
    }

    VIRTUAL_FS_VOL = tmp;

    VIRTUAL_FS_VOL[MAX_VIRTUAL_FS_VOL].name     = strdup(name);
    VIRTUAL_FS_VOL[MAX_VIRTUAL_FS_VOL].alias    = NULL;
    VIRTUAL_FS_VOL[MAX_VIRTUAL_FS_VOL].prefix   = NULL;
    VIRTUAL_FS_VOL[MAX_VIRTUAL_FS_VOL].inserted = true;

    MAX_VIRTUAL_FS_VOL++;
}

void MountVirtualDevices() {
    VirtualMountDevice("fs:/");
    if (gSystemFilesAllowed) {
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
    AddVirtualFSPath("vol", NULL, NULL);
    AddVirtualFSVOLPath("external01", NULL, NULL);
    AddVirtualFSVOLPath("content", NULL, NULL);

    VirtualMountDevice("sd:/");
}

void UnmountVirtualPaths() {
    uint32_t i = 0;
    for (i = 0; i < MAX_VIRTUAL_PARTITIONS; i++) {
        if (VIRTUAL_PARTITIONS[i].name) {
            free(VIRTUAL_PARTITIONS[i].name);
            VIRTUAL_PARTITIONS[i].name = NULL;
        }
        if (VIRTUAL_PARTITIONS[i].alias) {
            free(VIRTUAL_PARTITIONS[i].alias);
            VIRTUAL_PARTITIONS[i].name = NULL;
        }
        if (VIRTUAL_PARTITIONS[i].prefix) {
            free(VIRTUAL_PARTITIONS[i].prefix);
            VIRTUAL_PARTITIONS[i].prefix = NULL;
        }
    }

    for (i = 0; i < MAX_VIRTUAL_FS_VOL; i++) {
        if (VIRTUAL_FS_VOL[i].name) {
            free(VIRTUAL_FS_VOL[i].name);
            VIRTUAL_FS_VOL[i].name = NULL;
        }
        if (VIRTUAL_FS_VOL[i].alias) {
            free(VIRTUAL_FS_VOL[i].alias);
            VIRTUAL_FS_VOL[i].name = NULL;
        }
        if (VIRTUAL_FS_VOL[i].prefix) {
            free(VIRTUAL_FS_VOL[i].prefix);
            VIRTUAL_FS_VOL[i].prefix = NULL;
        }
    }

    for (i = 0; i < MAX_VIRTUAL_FS; i++) {
        if (VIRTUAL_FS[i].name) {
            free(VIRTUAL_FS[i].name);
            VIRTUAL_FS[i].name = NULL;
        }
        if (VIRTUAL_FS[i].alias) {
            free(VIRTUAL_FS[i].alias);
            VIRTUAL_FS[i].name = NULL;
        }
        if (VIRTUAL_FS[i].prefix) {
            free(VIRTUAL_FS[i].prefix);
            VIRTUAL_FS[i].prefix = NULL;
        }
    }

    if (VIRTUAL_PARTITIONS) {
        free(VIRTUAL_PARTITIONS);
        VIRTUAL_PARTITIONS = NULL;
    }
    if (VIRTUAL_FS_VOL) {
        free(VIRTUAL_FS_VOL);
        VIRTUAL_FS_VOL = NULL;
    }
    if (VIRTUAL_FS) {
        free(VIRTUAL_FS);
        VIRTUAL_FS = NULL;
    }
    VIRTUAL_PARTITIONS     = NULL;
    VIRTUAL_FS_VOL         = NULL;
    VIRTUAL_FS             = NULL;
    MAX_VIRTUAL_PARTITIONS = 0;
    MAX_VIRTUAL_FS         = 0;
    MAX_VIRTUAL_FS_VOL     = 0;
}
