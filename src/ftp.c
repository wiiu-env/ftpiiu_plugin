/*

ftpii -- an FTP server for the Wii

Copyright (C) 2008 Joseph Jordan <joe.ftpii@psychlaw.com.au>

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from
the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1.The origin of this software must not be misrepresented; you must not
claim that you wrote the original software. If you use this software in a
product, an acknowledgment in the product documentation would be
appreciated but is not required.

2.Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original software.

3.This notice may not be removed or altered from any source distribution.

*/
#include <coreinit/thread.h>
#include <errno.h>
#include <inttypes.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dir.h>
#include <sys/param.h>
#include <unistd.h>

#include "ftp.h"
#include "net.h"
#include "utils/logger.h"
#include "virtualpath.h"
#include "vrt.h"

#define BUS_SPEED 248625000

static const uint16_t SRC_PORT    = 20;
static const int32_t EQUIT        = 696969;
static const char *CRLF           = "\r\n";
static const uint32_t CRLF_LENGTH = 2;

static uint16_t passive_port = 1024;
static char *password        = NULL;

// for benchmarking purpose
static uint64_t startTime               = 0;
static volatile int32_t nbFilesReceived = 0;
static volatile int32_t nbFilesSent     = 0;

// priority of curent thread (when starting server)
static int32_t mainPriority = 16;

#define console_printf(FMT, ARGS...) DEBUG_FUNCTION_LINE_WRITE(FMT, ##ARGS);

// array of clients
static client_t WUT_ALIGNAS(64) * clients[MAX_CLIENTS] = {NULL};

// max and min transfer rate speeds in MBs
static float maxTransferRate = -9999;
static float minTransferRate = 9999;
// to avoid false speed estimation
static int maxSpeedPerTransfer = 6;

// sum of average speeds
static float sumAvgSpeed = 0;
// last sum of average speeds
static float lastSumAvgSpeed = 0;
// number of measures used for average computation
static uint32_t nbSpeedMeasures = 0;

// counters for active transfer on cpu (to set the priority of thread to launch)
static volatile uint8_t nbTransferOnCpu[3] = {0, 0, 0};

static void resetClient(client_t *client) {

    client->data_socket                  = -1;
    client->restart_marker               = 0;
    client->data_connection_connected    = false;
    client->data_callback                = NULL;
    client->data_connection_callback_arg = NULL;
    client->data_connection_cleanup      = NULL;
    client->data_connection_timer        = 0;
    client->f                            = NULL;
    strcpy(client->fileName, "");
    strcpy(client->uploadFilePath, "");
    client->bytesTransferred = -1;
    client->transferCallback = -EAGAIN;
}

// initialize the client without setting the index data member
static void initClient(client_t *client) {

    resetClient(client);

    client->representation_type = 'A';
    client->passive_socket      = -1;
    client->authenticated       = false;
    client->offset              = 0;
    strcpy(client->buf, "");
    strcpy(client->cwd, "/");
    strcpy(client->pending_rename, "");

    client->passive_socket = -1;
    client->socket         = -1;

    client->speed = 0;
}


int32_t create_server(uint16_t port) {

    // get the current thread (on CPU1)
    OSThread *thread = NULL;
    thread           = OSGetCurrentThread();
    if (thread != NULL) {
        // get priority of current thread
        mainPriority = OSGetThreadPriority(thread);
    }
    int32_t server = network_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (server < 0) {
        return -1;
    }

    struct sockaddr_in bindAddress;
    memset(&bindAddress, 0, sizeof(bindAddress));
    bindAddress.sin_family      = AF_INET;
    bindAddress.sin_port        = htons(port);
    bindAddress.sin_addr.s_addr = htonl(INADDR_ANY);

    int32_t ret;
    if ((ret = network_bind(server, (struct sockaddr *) &bindAddress, sizeof(bindAddress))) < 0) {
        network_close_blocking(server);
        //gxprintf("Error binding socket: [%i] %s\n", -ret, strerror(-ret));
        return ret;
    }
    if ((ret = network_listen(server, MAX_CLIENTS)) < 0) {
        network_close_blocking(server);
        //gxprintf("Error listening on socket: [%i] %s\n", -ret, strerror(-ret));
        return ret;
    }

    uint32_t client_index;

    for (client_index = 0; client_index < MAX_CLIENTS; client_index++) {

        // allocate clients[client_index]
        clients[client_index] = (client_t *) memalign(64, sizeof(client_t));
        if (!clients[client_index]) {
            console_printf("ERROR when allocating clients[%d]", client_index);
            return -ENOMEM;
        }
        initClient(clients[client_index]);

        // allocate the transfer thread
        clients[client_index]->transferThread = (OSThread *) memalign(32, sizeof(OSThread));
        if (!clients[client_index]->transferThread) {
            console_printf("ERROR when allocating transferThread [%d]", client_index);
            return -ENOMEM;
        }

        // allocate the transfer buffer
        clients[client_index]->transferBuffer = (char *) memalign(64, 2 * DEFAULT_NET_BUFFER_SIZE);
        if (!clients[client_index]->transferBuffer) {
            console_printf("ERROR when allocating transferThread [%d]", client_index);
            return -ENOMEM;
        }

        clients[client_index]->index = client_index;
    }


    // compute start time
    startTime = OSGetTime();

    return server;
}

void set_ftp_password(char *new_password) {
    if (password) {
        free(password);
        password = NULL;
    }
    if (new_password) {
        password = malloc(strlen(new_password) + 1);
        if (!password)
            return;

        strcpy((char *) password, new_password);
    } else {
        password = NULL;
    }
}

static bool compare_ftp_password(char *password_attempt) {
    return !password || !strcmp((char *) password, password_attempt);
}


// Main of transfer Threads
int launchTransfer(int argc UNUSED, const char **argv) {

    int32_t result   = -101;
    client_t *client = (client_t *) argv;

    if (strlen(client->uploadFilePath) == 0) {
        result = send_from_file(client->data_socket, client);
        nbFilesSent++;

    } else {
        result = recv_to_file(client->data_socket, client);
        nbFilesReceived++;
    }

    return result;
}

// launch and monitor the transfer
static int32_t transfer(int32_t data_socket UNUSED, client_t *client) {

    int32_t result = -EAGAIN;
    // on the very first call
    if (client->bytesTransferred == -1) {

        // init bytes counter
        client->bytesTransferred = 0;
        // init callback value
        client->transferCallback = -EAGAIN;
        // init speed to 0
        client->speed = 0;

        // client->index+1 = 1,8,9 => CPU2 (main loop, browsing connection => index+1=1)
        enum OS_THREAD_ATTRIB cpu = OS_THREAD_ATTRIB_AFFINITY_CPU2;
        // client->index+1 = 2,4,6 => CPU0
        if (client->index == 1 || client->index == 3 || client->index == 5) cpu = OS_THREAD_ATTRIB_AFFINITY_CPU0;
        // client->index+1 = 3,5,7 => CPU1
        if (client->index == 2 || client->index == 4 || client->index == 6) cpu = OS_THREAD_ATTRIB_AFFINITY_CPU1;

        // default priority (lowest) = priority of the the first thread launch on each CPUs
        int priority = mainPriority + 6;
        // update the priority in function of the number of active threads (active transfers) on the CPU used
        if (cpu == OS_THREAD_ATTRIB_AFFINITY_CPU0) {
            if (nbTransferOnCpu[0] == 2) {
                priority = mainPriority + 1;
            } else {
                if (nbTransferOnCpu[0] == 1) priority = mainPriority + 4;
            }
            nbTransferOnCpu[0]++;
        } else {
            if (cpu == OS_THREAD_ATTRIB_AFFINITY_CPU1) {
                if (nbTransferOnCpu[1] == 1) {
                    priority = mainPriority + 1;
                } else {
                    if (nbTransferOnCpu[1] == 0) priority = mainPriority + 4;
                }
                nbTransferOnCpu[1]++;
            } else {
                // CPU 2
                if (nbTransferOnCpu[2] == 2) {
                    priority = mainPriority + 1;
                } else {
                    if (nbTransferOnCpu[2] == 1) priority = mainPriority + 4;
                }
                nbTransferOnCpu[2]++;
            }
        }
        // launching transfer thread
        if (!OSCreateThread(client->transferThread, launchTransfer, 1, (char *) client, client->transferThreadStack + FTP_TRANSFER_STACK_SIZE, FTP_TRANSFER_STACK_SIZE, priority, cpu)) {
            return -105;
        }

        OSResumeThread(client->transferThread);


    } else {
        // join the thread here only in case of error or the transfer is finished
        if (client->bytesTransferred <= 0) {
            OSJoinThread(client->transferThread, &result);
        } else
            result = client->transferCallback;
    }

    return result;
}

// this method is called on transfer success but also on transfer failure
static int32_t endTransfer(client_t *client) {
    int32_t result = 0;

    // cancel thread if still running
    if (!OSIsThreadTerminated(client->transferThread)) {
        console_printf("C[%d] transfer of %s aborted !", client->index + 1, client->fileName);

        OSCancelThread(client->transferThread);
    }

    // update nbTransferOnCpu
    // client->index+1 = 1,8,9 => CPU2 (main loop, browsing connection => index+1=1)
    // client->index+1 = 2,4,6 => CPU0
    // client->index+1 = 3,5,7 => CPU1
    if (client->index == 1 || client->index == 3 || client->index == 5) {
        nbTransferOnCpu[0]--;
    } else {
        if (client->index == 2 || client->index == 4 || client->index == 6) {
            nbTransferOnCpu[1]--;
        } else {
            nbTransferOnCpu[2]--;
        }
    }

    // close f
    if (client->f != NULL) fclose(client->f);

    return result;
}

// this method is called on transfer success but also on transfer failure
static int32_t endTransferOnSd(client_t *client) {

    // close f
    if (client->f != NULL) fclose(client->f);
    return 0;
}

/*
	TODO: support multi-line reply
*/
static int32_t write_reply(client_t *client, uint16_t code, char *msg) {
    uint32_t msglen = 4 + strlen(msg) + CRLF_LENGTH;
    char *msgbuf    = (char *) malloc(msglen + 1);
    if (msgbuf == NULL)
        return -ENOMEM;
    if (code == 211) {
        sprintf(msgbuf, "%u-%s\r\n", code, msg);
    } else {
        sprintf(msgbuf, "%u %s\r\n", code, msg);
    }
    console_printf("Wrote reply: %s", msgbuf);
    int32_t ret = send_exact(client->socket, msgbuf, msglen);
    free(msgbuf);
    msgbuf = NULL;
    return ret;
}


static void close_passive_socket(client_t *client) {
    if (client->passive_socket >= 0) {
        network_close_blocking(client->passive_socket);
        client->passive_socket = -1;
    }
}

/*
	result must be able to hold up to maxsplit+1 null-terminated strings of length strlen(s)
	returns the number of strings stored in the result array (up to maxsplit+1)
*/
static uint32_t split(char *s, char sep, uint32_t maxsplit, char *result[]) {
    uint32_t num_results = 0;
    uint32_t result_pos  = 0;
    uint32_t trim_pos    = 0;
    bool in_word         = false;
    for (; *s; s++) {
        if (*s == sep) {
            if (num_results <= maxsplit) {
                in_word = false;
                continue;
            } else if (!trim_pos) {
                trim_pos = result_pos;
            }
        } else if (trim_pos) {
            trim_pos = 0;
        }
        if (!in_word) {
            in_word = true;
            if (num_results <= maxsplit) {
                num_results++;
                result_pos = 0;
            }
        }
        result[num_results - 1][result_pos++] = *s;
        result[num_results - 1][result_pos]   = '\0';
    }
    if (trim_pos) {
        result[num_results - 1][trim_pos] = '\0';
    }
    uint32_t i = num_results;
    for (i = num_results; i <= maxsplit; i++) {
        result[i][0] = '\0';
    }
    return num_results;
}

static int32_t ftp_USER(client_t *client, char *username UNUSED) {
    return write_reply(client, 331, "User name okay, need password.");
}

static int32_t ftp_PASS(client_t *client, char *password_attempt) {
    if (compare_ftp_password(password_attempt)) {
        client->authenticated = true;
        return write_reply(client, 230, "User logged in, proceed.");
    } else {
        return write_reply(client, 530, "Login incorrect.");
    }
}

static int32_t ftp_REIN(client_t *client, char *rest UNUSED) {
    close_passive_socket(client);
    strcpy(client->cwd, "/");
    client->representation_type = 'A';
    client->authenticated       = false;
    return write_reply(client, 220, "Service ready for new user.");
}

static int32_t ftp_QUIT(client_t *client, char *rest UNUSED) {
    // TODO: dont quit if xfer in progress
    int32_t result = write_reply(client, 221, "Service closing control connection.");
    return result < 0 ? result : -EQUIT;
}

static int32_t ftp_SYST(client_t *client, char *rest UNUSED) {
    return write_reply(client, 215, "UNIX Type: L8 Version: ftpii");
}

static int32_t ftp_TYPE(client_t *client, char *rest) {
    char representation_type[FTP_BUFFER_SIZE], param[FTP_BUFFER_SIZE];
    char *args[]      = {representation_type, param};
    uint32_t num_args = split(rest, ' ', 1, args);
    if (num_args == 0) {
        return write_reply(client, 501, "Syntax error in parameters.");
    } else if ((!strcasecmp("A", representation_type) && (!*param || !strcasecmp("N", param))) ||
               (!strcasecmp("I", representation_type) && num_args == 1)) {
        client->representation_type = *representation_type;
    } else {
        return write_reply(client, 501, "Syntax error in parameters.");
    }
    char msg[FTP_BUFFER_SIZE + 30] = "";
    sprintf(msg, "C[%d] Type set to %s", client->index + 1, representation_type);
    return write_reply(client, 200, msg);
}

static int32_t ftp_MODE(client_t *client, char *rest) {
    if (!strcasecmp("S", rest)) {
        return write_reply(client, 200, "Mode S ok.");
    } else {
        return write_reply(client, 501, "Syntax error in parameters.");
    }
}

static int32_t ftp_FEAT(client_t *client, char *rest) {
    return write_reply(client, 211, "Features:\r\n UTF8\r\n211 End");
}

static int32_t ftp_OPTS(client_t *client, char *rest) {
    if (!strcasecmp("UTF8 ON", rest)) {
        return write_reply(client, 200, "OK");
    } else {
        return write_reply(client, 502, "Command not implemented.");
    }
}
// The three methods bellow are created to expand the FTP client support
// client->cwd and path are not filled in the same way for WinScp, FilleZilla and Cyberduck
// those functions are used to provide a same result for all clients :
//
// 		- client->cwd is the folder containing path and path is a file or folder name
//

// Remove eventually the last trailling slash of client->cwd
static void removeTrailingSlash(char **cwd) {
    if (strcmp(*cwd, ".") == 0) strcpy(*cwd, "/");
    else {
        if ((strlen(*cwd) > 0) && (strcmp(*cwd, "/") != 0)) {
            char *path = (char *) malloc(strlen(*cwd) + 1);
            strcpy(path, *cwd);
            char *pos = strrchr(path, '/');
            if (strcmp(pos, "/") == 0) {
                // folder is allocated by strndup
                char *folder = strndup(*cwd, strlen(path) - strlen(pos));
                // update cwd
                strcpy(*cwd, folder);
                free(folder);
            }
            free(path);
        }
    }
}

// basename(cwd) : return last folder in path
// caller must free the returned string
static char *basename(char *cwd) {
    char *final = NULL;
    if ((strlen(cwd) > 0) && (strcmp(cwd, "/") != 0)) {
        char *path = (char *) malloc(strlen(cwd) + 1);
        strcpy(path, cwd);
        char *pos = strrchr(path, '/');
        // final is allocated by strdup
        final = strdup(pos + 1);
        free(path);
    }
    return final;
}

// this method is used to format the path :
//   - client->cwd is the folder containing path
//   - path is a file or folder name
//
// (client->cwd and path are not filled in the same way for WinScp, FilleZilla and Cyberduck)
//
// caller must free folder and fileName strings
static void formatPath(char *cwd, char *path, char **folder, char **fileName) {

    *fileName = NULL;
    *folder   = NULL;
    char *pos = NULL;

    if ((strcmp(cwd, "/") == 0) && (path && ((strcmp(path, ".") == 0) || (strcmp(path, "/") == 0)))) {

        // allocate and copy folder with cwd
        *folder = (char *) malloc(strlen(cwd) + 1);
        strcpy(*folder, cwd);

        // allocate and copy fileName with .
        *fileName = (char *) malloc(strlen(path) + 1);
        strcpy(*fileName, ".");

    } else {

        char *cwdNoSlash = NULL;
        // allocate and copy fileName with path
        cwdNoSlash = (char *) malloc(strlen(cwd) + 1);
        strcpy(cwdNoSlash, cwd);

        // remove any trailing slash when cwd != "/"
        removeTrailingSlash(&cwdNoSlash);

        // cyberduck support
        if (path) {
            // path is given

            // if first char is a slash
            if ((path[0] == '/') && (strlen(path) > 1)) {
                // path gives the whole full path

                // get the folder from path
                *fileName = basename(path);
                pos       = strrchr(path, '/');
                // folder is allocated by strndup
                *folder = strndup(path, strlen(path) - strlen(pos));

            } else {

                if (strlen(path) > 1) {

                    // path gives file's name

                    // check if cwd contains path (cyberduck)
                    if (strstr(cwdNoSlash, path) != NULL) {
                        // remove file's name from the path

                        // get the fileName from cwd
                        *fileName = basename(cwdNoSlash);
                        pos       = strrchr(cwdNoSlash, '/');
                        // folder is allocated by strndup
                        *folder = strndup(cwdNoSlash, strlen(cwdNoSlash) - strlen(pos));

                        // update cwd
                        strcpy(cwd, *folder);
                        strcat(cwd, "/");

                    } else {

                        if (strcmp(cwdNoSlash, "") != 0) {

                            // allocate and copy fileName with path
                            *fileName = (char *) malloc(strlen(path) + 1);
                            strcpy(*fileName, path);
                            // allocate and copy folder with cwd
                            *folder = (char *) malloc(strlen(cwdNoSlash) + 1);
                            strcpy(*folder, cwdNoSlash);

                        } else {

                            // get the folder from path
                            *fileName = basename(path);
                            pos       = strrchr(path, '/');
                            // folder is allocated by strndup
                            *folder = strndup(path, strlen(path) - strlen(pos));

                            // update cwd
                            strcpy(cwd, *folder);
                            strcat(cwd, "/");
                            // update path
                            strcpy(path, *fileName);
                        }
                    }
                } else {
                    if (strlen(path) == 1) {
                        if (strcmp(path, "/") == 0) {

                            *folder = (char *) malloc(2);
                            strcpy(*folder, "/");
                            *fileName = (char *) malloc(2);
                            strcpy(*fileName, ".");

                        } else {
                            if (strcmp(path, ".") != 0) {
                                *folder = (char *) malloc(2);
                                strcpy(*folder, cwd);
                                *fileName = (char *) malloc(2);
                                strcpy(*fileName, path);
                            }
                        }
                        // else should be '.'
                    } else {

                        // path is not given, cwd gives the whole full path
                        if (strcmp(cwd, "/") != 0) {

                            // get path from cwd
                            path      = basename(cwdNoSlash);
                            *fileName = (char *) malloc(strlen(path) + 1);
                            strcpy(*fileName, path);

                            pos = strrchr(cwdNoSlash, '/');
                            // folder is allocated by strndup
                            *folder = strndup(cwdNoSlash, strlen(cwdNoSlash) - strlen(pos));

                            // update cwd
                            strcpy(cwd, *folder);
                            strcat(cwd, "/");
                        }
                    }
                }
            }

        } else {

            // path is not given, cwd gives the whole full path
            if (strcmp(cwd, "/") != 0) {


                // path is not given, cwd gives the whole full path
                // get path from cwd
                path      = basename(cwdNoSlash);
                *fileName = (char *) malloc(strlen(path) + 1);
                strcpy(*fileName, path);

                pos = strrchr(cwdNoSlash, '/');
                // folder is allocated by strndup
                *folder = strndup(cwdNoSlash, strlen(path) - strlen(pos));

                // update cwd
                strcpy(cwd, *folder);
                strcat(cwd, "/");
            }
        }
    }
}
static int32_t ftp_PWD(client_t *client, char *rest UNUSED) {
    char msg[FTPMAXPATHLEN + 24];
    // TODO: escape double-quotes

    // FTP client like cyberduck fail to get the path used for CWD (next command after PWD when login) from ftp_CWD's response
    // => the msg to be sent to FTP client must begin with client->cwd !
    if (strrchr(client->cwd, '"')) {
        sprintf(msg, "%s is current directory", client->cwd);
    } else {
        sprintf(msg, "\"%s\" is current directory", client->cwd);
    }

    return write_reply(client, 257, msg);
}

static int32_t ftp_CWD(client_t *client, char *path) {
    int32_t result = 0;
    char *folder   = NULL;
    char *fileName = NULL;

    formatPath(client->cwd, path, &folder, &fileName);

    strcpy(client->cwd, folder);
    if (strcmp(client->cwd, "/") != 0) strcat(client->cwd, "/");


    if (fileName != NULL) {

        if (!vrt_chdir(client->cwd, fileName)) {

            char msg[FTPMAXPATHLEN + 60] = "";
            sprintf(msg, "C[%d] CWD successful to %s", client->index + 1, client->cwd);
            write_reply(client, 250, msg);
        } else {

            char msg[FTPMAXPATHLEN + 40] = "";
            sprintf(msg, "C[%d] error when CWD to cwd=%s path=%s : err=%s", client->index + 1, client->cwd, fileName, strerror(errno));

            write_reply(client, 550, msg);
        }
        free(fileName);
    }
    if (folder != NULL) free(folder);

    // always return 0 on server side
    // - when client needs to create a folder tree on server side, client try CWD until it do not fail before launching the MKD command)
    // - note that when ftp_CWD fails, an error is sent to the client with the 550 error code
    return result;
}

static int32_t ftp_CDUP(client_t *client, char *rest UNUSED) {
    int32_t result               = 0;
    char msg[FTPMAXPATHLEN + 40] = "";

    if (!vrt_chdir(client->cwd, "..")) {
        sprintf(msg, "C[%d] CDUP command successful", client->index + 1);
        result = write_reply(client, 250, "CDUP command successful.");
    } else {
        sprintf(msg, "C[%d] Error when CDUP to %s : err = %s", client->index + 1, client->cwd, strerror(errno));
        result = write_reply(client, 550, strerror(errno));
    }
    return result;
}

static int32_t ftp_DELE(client_t *client, char *path) {
    char *folder   = NULL;
    char *fileName = NULL;

    formatPath(client->cwd, path, &folder, &fileName);
    strcpy(client->cwd, folder);
    if (strcmp(client->cwd, "/") != 0) strcat(client->cwd, "/");
    char msg[FTPMAXPATHLEN + 40] = "";
    int msgCode                  = 550;
    if (!vrt_unlink(client->cwd, fileName)) {
        sprintf(msg, "C[%d] %s removed", client->index + 1, fileName);
        msgCode = 250;
    } else {
        sprintf(msg, "C[%d] Error when DELE %s/%s : err = %s", client->index + 1, client->cwd, fileName, strerror(errno));
    }
    if (fileName != NULL) free(fileName);
    if (folder != NULL) free(folder);
    return write_reply(client, msgCode, msg);
}

static int32_t ftp_MKD(client_t *client, char *path) {
    if (!*path) {
        return write_reply(client, 501, "Syntax error in parameters.");
    }
    char *folder   = NULL;
    char *fileName = NULL;

    formatPath(client->cwd, path, &folder, &fileName);
    strcpy(client->cwd, folder);
    if (strcmp(client->cwd, "/") != 0) strcat(client->cwd, "/");

    char msg[FTPMAXPATHLEN + 60] = "";
    int msgCode                  = 550;

    if (vrt_checkdir(client->cwd, fileName) >= 0) {
        msgCode = 257;
        sprintf(msg, "C[%d] folder already exist", client->index + 1);

    } else {

        if (!vrt_mkdir(client->cwd, fileName, 0775)) {
            msgCode = 250;
            sprintf(msg, "C[%d] directory %s created in %s", client->index + 1, fileName, client->cwd);

        } else {
            sprintf(msg, "C[%d] Error in MKD when cd to cwd=%s, path=%s : err = %s", client->index + 1, client->cwd, fileName, strerror(errno));
        }
    }

    if (fileName != NULL) free(fileName);
    if (folder != NULL) free(folder);
    return write_reply(client, msgCode, msg);
}

static int32_t ftp_RNFR(client_t *client, char *path) {
    char *folder   = NULL;
    char *fileName = NULL;

    formatPath(client->cwd, path, &folder, &fileName);
    strcpy(client->fileName, fileName);
    strcpy(client->cwd, folder);
    if (strcmp(client->cwd, "/") != 0) strcat(client->cwd, "/");

    strcpy(client->pending_rename, fileName);
    char msg[FTPMAXPATHLEN + 24] = "";
    sprintf(msg, "C[%d] Ready for RNTO for %s", client->index + 1, fileName);

    if (fileName != NULL) free(fileName);
    if (folder != NULL) free(folder);

    return write_reply(client, 350, msg);
}

static int32_t ftp_RNTO(client_t *client, char *path) {
    char msg[FTPMAXPATHLEN + 60] = "";
    if (!*client->pending_rename) {
        sprintf(msg, "C[%d] RNFR required first", client->index + 1);
        return write_reply(client, 503, msg);
    }
    int32_t result = 0;
    char *folder   = NULL;
    char *fileName = NULL;

    formatPath(client->cwd, path, &folder, &fileName);
    strcpy(client->cwd, folder);
    if (strcmp(client->cwd, "/") != 0) strcat(client->cwd, "/");
    if (!vrt_rename(client->cwd, client->pending_rename, fileName)) {
        sprintf(msg, "C[%d] Rename %s to %s successfully", client->index + 1, client->pending_rename, path);
        result = write_reply(client, 250, msg);
    } else {
        sprintf(msg, "C[%d] failed to rename %s to %s : err = %s", client->index + 1, client->pending_rename, fileName, strerror(errno));

        result = write_reply(client, 550, msg);
    }
    *client->pending_rename = '\0';

    if (fileName != NULL) free(fileName);
    if (folder != NULL) free(folder);

    return result;
}

static int32_t ftp_SIZE(client_t *client, char *path) {
    struct stat st;

    char *folder   = NULL;
    char *fileName = NULL;

    formatPath(client->cwd, path, &folder, &fileName);
    strcpy(client->cwd, folder);
    if (strcmp(client->cwd, "/") != 0) strcat(client->cwd, "/");

    char msg[FTPMAXPATHLEN + 40] = "";
    int msgCode                  = 550;
    if (!vrt_stat(client->cwd, fileName, &st)) {
        sprintf(msg, "%llu", st.st_size);
        msgCode = 213;
    } else {
        sprintf(msg, "C[%d] Error SIZE on %s in %s : err=%s", client->index + 1, fileName, client->cwd, strerror(errno));
    }
    if (fileName != NULL) free(fileName);
    if (folder != NULL) free(folder);
    return write_reply(client, msgCode, msg);
}

static int32_t ftp_PASV(client_t *client, char *rest UNUSED) {

    close_passive_socket(client);

    int nbTries = 0;
    while (1) {
        client->passive_socket = network_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (client->passive_socket >= 0) break;
        OSSleepTicks(OSMillisecondsToTicks(MAX_CLIENTS * 20));
        if (++nbTries > FTP_RETRIES_NUMBER) {
            char msg[FTP_BUFFER_SIZE];
            sprintf(msg, "C[%d] failed to create passive socket (%d), err = %d (%s)", client->index + 1, client->passive_socket, errno, strerror(errno));
            console_printf("~ WARNING : %s", msg);
            return write_reply(client, 520, msg);
        }
    }

    struct sockaddr_in bindAddress;
    memset(&bindAddress, 0, sizeof(bindAddress));
    bindAddress.sin_family = AF_INET;
    // reset passive_port to avoid overflow
    if (passive_port == 65534) {
        passive_port = 1024;
    } else {
        passive_port++;
    }
    bindAddress.sin_port        = htons(passive_port);
    bindAddress.sin_addr.s_addr = htonl(INADDR_ANY);

    nbTries = 0;
    while (1) {
        int32_t result;
        result = network_bind(client->passive_socket, (struct sockaddr *) &bindAddress, sizeof(bindAddress));
        if (result >= 0)
            break;
        OSSleepTicks(OSMillisecondsToTicks(MAX_CLIENTS * 20));
        if (++nbTries > FTP_RETRIES_NUMBER) {
            char msg[FTP_BUFFER_SIZE] = "";
            sprintf(msg, "C[%d] failed to bind passive socket (%d), err = %d (%s)", client->index + 1, client->passive_socket, errno, strerror(errno));
            console_printf("~ WARNING : %s", msg);
            close_passive_socket(client);
            return write_reply(client, 520, msg);
        }
    }

    nbTries = 0;
    while (1) {
        int32_t result;
        result = network_listen(client->passive_socket, 1);

        if (result >= 0)
            break;
        OSSleepTicks(OSMillisecondsToTicks(MAX_CLIENTS * 20));
        if (++nbTries > FTP_RETRIES_NUMBER) {
            char msg[FTP_BUFFER_SIZE] = "";
            sprintf(msg, "C[%d] failed to listen on passive socket (%d), err = %d (%s)", client->index + 1, client->passive_socket, errno, strerror(errno));
            console_printf("~ WARNING : %s", msg);
            close_passive_socket(client);
            return write_reply(client, 520, msg);
        }
    }
    char reply[49 + 2 + 16]    = "";
    uint16_t port              = bindAddress.sin_port;
    uint32_t ip                = network_gethostip();
    struct in_addr UNUSED addr = {};
    addr.s_addr                = ip;
    console_printf("C[%d] : listening for data connections at %s:%u...\n", client->index + 1, inet_ntoa(addr), port);
    sprintf(reply, "C[%d] entering Passive Mode (%u,%u,%u,%u,%u,%u).", client->index + 1, (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff, (port >> 8) & 0xff, port & 0xff);
    return write_reply(client, 227, reply);
}

static int32_t ftp_PORT(client_t *client, char *portspec) {
    uint32_t h1, h2, h3, h4, p1, p2;
    if (sscanf(portspec, "%3u,%3u,%3u,%3u,%3u,%3u", &h1, &h2, &h3, &h4, &p1, &p2) < 6) {
        return write_reply(client, 501, "Syntax error in parameters.");
    }
    char addr_str[44];
    sprintf(addr_str, "%u.%u.%u.%u", h1, h2, h3, h4);
    struct in_addr sin_addr;
    if (!inet_aton(addr_str, &sin_addr)) {
        return write_reply(client, 501, "Syntax error in parameters.");
    }
    close_passive_socket(client);
    uint16_t port            = ((p1 & 0xff) << 8) | (p2 & 0xff);
    client->address.sin_addr = sin_addr;
    client->address.sin_port = htons(port);
    console_printf("Set client address to %s:%u\n", addr_str, port);
    return write_reply(client, 200, "PORT command successful.");
}

typedef int32_t (*data_connection_handler)(client_t *client, data_connection_callback callback, void *arg);

static int32_t prepare_data_connection_active(client_t *client, data_connection_callback callback UNUSED, void *arg UNUSED) {

    int nbTries = 0;

    while (1) {
        client->data_socket = network_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (client->data_socket >= 0)
            break;
        OSSleepTicks(OSMillisecondsToTicks(MAX_CLIENTS * 20));
        if (++nbTries > FTP_RETRIES_NUMBER) {
            char msg[FTP_BUFFER_SIZE] = "";
            sprintf(msg, "C[%d] failed to create data socket (%d), err = %d (%s)", client->index + 1, client->data_socket, errno, strerror(errno));
            console_printf("~ WARNING : %s", msg);
            return write_reply(client, 520, msg);
        }
    }
    struct sockaddr_in bindAddress;
    memset(&bindAddress, 0, sizeof(bindAddress));
    bindAddress.sin_family      = AF_INET;
    bindAddress.sin_port        = htons(SRC_PORT);
    bindAddress.sin_addr.s_addr = htonl(INADDR_ANY);

    nbTries = 0;
    while (1) {
        int32_t result;
        result = network_bind(client->data_socket, (struct sockaddr *) &bindAddress, sizeof(bindAddress));
        if (result >= 0)
            break;
        OSSleepTicks(OSMillisecondsToTicks(MAX_CLIENTS * 20));
        if (++nbTries > FTP_RETRIES_NUMBER) {
            char msg[FTP_BUFFER_SIZE] = "";
            sprintf(msg, "C[%d] failed to bind data socket (%d), err = %d (%s)", client->index + 1, client->data_socket, errno, strerror(errno));
            console_printf("~ WARNING : %s", msg);
            network_close_blocking(client->data_socket);
            client->data_socket = -1;
            return write_reply(client, 520, msg);
        }
    }

    console_printf("Attempting to connect to client at %s:%u\n", inet_ntoa(client->address.sin_addr), client->address.sin_port);
    return 0;
}

static int32_t prepare_data_connection_passive(client_t *client, data_connection_callback callback UNUSED, void *arg UNUSED) {
    client->data_socket = client->passive_socket;
    console_printf("Waiting for data connections...\n");
    return 0;
}

static int32_t prepare_data_connection(client_t *client, void *callback, void *arg, void *cleanup) {
    char msgStatus[FTPMAXPATHLEN + 60] = "";
    sprintf(msgStatus, "C[%d] connected", client->index + 1);

    int32_t result = write_reply(client, 150, msgStatus);
    if (result >= 0) {
        data_connection_handler handler = prepare_data_connection_active;
        if (client->passive_socket >= 0)
            handler = prepare_data_connection_passive;
        result = handler(client, (data_connection_callback) callback, arg);
        if (result < 0) {
            char msg[FTPMAXPATHLEN + 50] = "";
            sprintf(msg, "Closing C[%d], transfer failed (%s)", client->index + 1, client->fileName);
            result = write_reply(client, 520, msg);
        } else {
            client->data_connection_connected    = false;
            client->data_callback                = callback;
            client->data_connection_callback_arg = arg;
            client->data_connection_cleanup      = cleanup;
            // timestamp uint64 in nanoseconds
            client->data_connection_timer = OSGetTime() + (OSTime) (FTP_SERVER_CONNECTION_TIMEOUT) *1000000000;
        }
    }
    return result;
}

static int32_t send_nlst(int32_t data_socket, DIR_P *iter) {
    int32_t result               = 0;
    char filename[FTPMAXPATHLEN] = "";
    struct dirent *dirent        = NULL;
    while ((dirent = vrt_readdir(iter)) != 0) {
        size_t end_index = strlen(dirent->d_name);
        if (end_index + 2 >= FTPMAXPATHLEN)
            continue;
        strcpy(filename, dirent->d_name);
        filename[end_index]     = CRLF[0];
        filename[end_index + 1] = CRLF[1];
        filename[end_index + 2] = '\0';
        if ((result = send_exact(data_socket, filename, strlen(filename))) < 0) {
            break;
        }
    }
    return result < 0 ? result : 0;
}

static int32_t send_list(int32_t data_socket, DIR_P *iter) {
    struct stat st;
    int32_t result = 0;
    time_t mtime   = 0;
    uint64_t size  = 0;
    // use MAXPATHLEN instead of FTPMAXPATHLEN becoause of dirent->d_name length (to avoid warning on build)
    char filename[MAXPATHLEN] = "";
    char line[MAXPATHLEN + 56 + CRLF_LENGTH + 1];
    struct dirent *dirent = NULL;
    while ((dirent = vrt_readdir(iter)) != 0) {

        snprintf(filename, sizeof(filename), "%s/%s", iter->path, dirent->d_name);
        if (stat(filename, &st) == 0) {
            mtime = st.st_mtime;
            size  = st.st_size;
        } else {
            mtime = time(0);
            size  = 0;
        }

        // if the date is past 2040-01-01 then use the current date instead.
        if (mtime > 0x2208985200L) {
            mtime = time(0);
        }

        char timestamp[13];
        strftime(timestamp, sizeof(timestamp), "%b %d  %Y", localtime(&mtime));
        snprintf(line, sizeof(line), "%c%s%s%s%s%s%s%s%s%s    1 USER     WII-U %10llu %s %s\r\n", (dirent->d_type & DT_DIR) ? 'd' : '-',
                 S_ISLNK(st.st_mode) ? "l" : (st.st_mode & S_IRUSR ? "r" : "-"),
                 st.st_mode & S_IWUSR ? "w" : "-",
                 st.st_mode & S_IXUSR ? "x" : "-",
                 st.st_mode & S_IRGRP ? "r" : "-",
                 st.st_mode & S_IWGRP ? "w" : "-",
                 st.st_mode & S_IXGRP ? "x" : "-",
                 st.st_mode & S_IROTH ? "r" : "-",
                 st.st_mode & S_IWOTH ? "w" : "-",
                 st.st_mode & S_IXOTH ? "x" : "-",
                 size, timestamp, dirent->d_name);

        // check that the line does not exceed FTP_BUFFER_SIZE
        if (strlen(line) < FTP_BUFFER_SIZE) {
            if ((result = send_exact(data_socket, line, strlen(line))) < 0) {
                break;
            }
        } else {
            console_printf("ERROR : line exceed %d, skip sending", FTP_BUFFER_SIZE);
            console_printf("line = [%s]", line);
            return -EINVAL;
        }
    }
    return result < 0 ? result : 0;
}

static int32_t ftp_NLST(client_t *client, char *path) {
    if (!*path) {
        path = ".";
    }

    DIR_P *dir = vrt_opendir(client->cwd, path);
    if (dir == NULL) {
        char msg[FTPMAXPATHLEN + 40] = "";
        sprintf(msg, "Error when NLIST cwd=%s path=%s : err = %s", client->cwd, path, strerror(errno));
        return write_reply(client, 550, msg);
    }

    int32_t result = prepare_data_connection(client, send_nlst, dir, vrt_closedir);
    if (result < 0)
        vrt_closedir(dir);
    return result;
}

static int32_t ftp_LIST(client_t *client, char *path) {
    char rest[FTP_BUFFER_SIZE] = "";
    if (*path == '-') {
        // handle buggy clients that use "LIST -aL" or similar, at the expense of breaking paths that begin with '-'
        char flags[FTP_BUFFER_SIZE];
        char *args[] = {flags, rest};
        split(path, ' ', 1, args);
        path = rest;
    }
    if (!*path) {
        path = ".";
    }

    if (path && (strlen(client->cwd) != 0)) {
        if (strcmp(path, ".") == 0 && strcmp(client->cwd, "/") == 0) {
            UnmountVirtualPaths();
            MountVirtualDevices();
        }
    }

    DIR_P *dir = vrt_opendir(client->cwd, path);
    if (dir == NULL) {
        char msg[FTPMAXPATHLEN + 40] = "";
        sprintf(msg, "Error when LIST cwd=%s path=%s : err = %s", client->cwd, path, strerror(errno));
        return write_reply(client, 550, msg);
    }

    int32_t result = prepare_data_connection(client, send_list, dir, vrt_closedir);
    if (result < 0)
        vrt_closedir(dir);
    return result;
}

static int32_t ftp_RETR(client_t *client, char *path) {
    char *folder   = NULL;
    char *fileName = NULL;


    formatPath(client->cwd, path, &folder, &fileName);
    strcat(folder, "/");

    strcpy(client->fileName, fileName);
    strcpy(client->cwd, folder);

    if (fileName != NULL) free(fileName);
    if (folder != NULL) free(folder);

    client->f = vrt_fopen(client->cwd, client->fileName, "rb");
    if (!client->f) {
        char msg[FTPMAXPATHLEN + 40] = "";
        sprintf(msg, "C[%d] Error sending cwd=%s path=%s : err=%s", client->index + 1, client->cwd, path, strerror(errno));
        return write_reply(client, 550, msg);
    }
    bool transferOnSdcard = false;
    if (strcmp(client->cwd, "/sd/") >= 0) transferOnSdcard = true;

    if (!transferOnSdcard)
        // set the size to TRANSFER_BUFFER_SIZE (chunk size used in send_from_file)
        setvbuf(client->f, client->transferBuffer, _IOFBF, DEFAULT_NET_BUFFER_SIZE);

    int fd = fileno(client->f);
    // if client->restart_marker <> 0; check its value
    if (client->restart_marker && lseek(fd, client->restart_marker, SEEK_SET) != client->restart_marker) {
        int32_t lseek_error = errno;
        fclose(client->f);
        // reset the marker to 0
        client->restart_marker = 0;
        return write_reply(client, 550, strerror(lseek_error));
    }

    // hard limit transfers on SD card
    int32_t result = 0;
    if (transferOnSdcard) {
        result = prepare_data_connection(client, send_from_file, client, endTransferOnSd);
        if (result < 0) endTransferOnSd(client);
    } else {
        result = prepare_data_connection(client, transfer, client, endTransfer);
        if (result < 0) endTransfer(client);
    }

    return result;
}

static int32_t stor_or_append(client_t *client, char *path, char mode[3]) {

    // Fix  Feature Request: Recursively create directories #26
    // Handlers are launched asynchronously => ftp_STOR or ftp_APPE can be launched
    // before ftp_MKD on file's folder
    char *folder   = NULL;
    char *fileName = NULL;

    formatPath(client->cwd, path, &folder, &fileName);
    strcpy(client->fileName, fileName);
    strcpy(client->cwd, folder);
    if (strcmp(client->cwd, "/") != 0) strcat(client->cwd, "/");

    // store the file path
    sprintf(client->uploadFilePath, "%s%s", folder, fileName);

    char *folderName   = basename(folder);
    char *pos          = strrchr(folder, '/');
    char *parentFolder = strndup(folder, strlen(folder) - strlen(pos) + 1);

    if (vrt_checkdir(parentFolder, folderName)) {
        vrt_mkdir(parentFolder, folderName, 0775);
    }

    if (fileName != NULL) free(fileName);
    if (folder != NULL) free(folder);
    if (parentFolder != NULL) free(parentFolder);
    if (folderName != NULL) free(folderName);

    client->f = vrt_fopen(client->cwd, client->fileName, mode);
    if (!client->f) {

        char msg[FTPMAXPATHLEN + 40] = "";
        sprintf(msg, "C[%d] Error storing cwd=%s path=%s : err=%s", client->index + 1, client->cwd, path, strerror(errno));
        return write_reply(client, 550, msg);
    }

    bool transferOnSdcard = false;
    if (strcmp(client->cwd, "/sd/") >= 0) transferOnSdcard = true;

    if (!transferOnSdcard)
        // set the size to DEFAULT_NET_BUFFER_SIZE (chunk size used in recv_to__file)
        setvbuf(client->f, client->transferBuffer, _IOFBF, DEFAULT_NET_BUFFER_SIZE);

    // hard limit transfers on SD card
    int32_t result = 0;
    if (transferOnSdcard) {
        result = prepare_data_connection(client, recv_to_file, client, endTransferOnSd);
        if (result < 0) endTransferOnSd(client);
    } else {
        result = prepare_data_connection(client, transfer, client, endTransfer);
        if (result < 0) endTransfer(client);
    }

    return result;
}

// called by client when replacing a file
static int32_t ftp_STOR(client_t *client, char *path) {

    client->restart_marker = 0;

    return stor_or_append(client, path, "wb");
}

// called by client when resuming a file
static int32_t ftp_APPE(client_t *client, char *path) {

    return stor_or_append(client, path, "ab");
}

static int32_t ftp_REST(client_t *client, char *offset_str) {
    off_t offset;
    if (sscanf(offset_str, "%lli", &offset) < 1 || offset < 0) {
        return write_reply(client, 501, "Syntax error in parameters.");
    }
    client->restart_marker        = offset;
    char msg[FTPMAXPATHLEN + 100] = "";
    sprintf(msg, "C[%d] restart position accepted (%lli) for %s", client->index + 1, offset, client->fileName);
    return write_reply(client, 350, msg);
}

static int32_t ftp_SITE_LOADER(client_t *client, char *rest UNUSED) {
    int32_t result = write_reply(client, 200, "Exiting to loader.");
    //set_reset_flag();
    return result;
}

static int32_t ftp_SITE_CLEAR(client_t *client, char *rest UNUSED) {
    int32_t result = write_reply(client, 200, "Cleared.");
    uint32_t i;
    for (i = 0; i < 18; i++)
        console_printf("\n");
    //console_printf("\x1b[2;0H");
    return result;
}

/*
	This is implemented as a no-op to prevent some FTP clients
	from displaying skip/abort/retry type prompts.
*/
static int32_t ftp_SITE_CHMOD(client_t *client, char *rest UNUSED) {
    char msg[FTPMAXPATHLEN + 50] = "";
    sprintf(msg, "C[%d] CHMOD %s successfully", client->index + 1, client->cwd);

    return write_reply(client, 250, msg);
}

static int32_t ftp_SITE_PASSWD(client_t *client, char *new_password) {
    set_ftp_password(new_password);
    return write_reply(client, 200, "Password changed.");
}

static int32_t ftp_SITE_NOPASSWD(client_t *client, char *rest UNUSED) {
    set_ftp_password(NULL);
    return write_reply(client, 200, "Authentication disabled.");
}

static int32_t ftp_SITE_EJECT(client_t *client, char *rest UNUSED) {
    //if (dvd_eject()) return write_reply(client, 550, "Unable to eject DVD.");
    return write_reply(client, 200, "DVD ejected.");
}

static int32_t ftp_SITE_MOUNT(client_t *client, char *path UNUSED) {
    //if (!mount_virtual(path)) return write_reply(client, 550, "Unable to mount.");
    return write_reply(client, 250, "Mounted.");
}

static int32_t ftp_SITE_UNMOUNT(client_t *client, char *path UNUSED) {
    //if (!unmount_virtual(path)) return write_reply(client, 550, "Unable to unmount.");
    return write_reply(client, 250, "Unmounted.");
}

static int32_t ftp_SITE_UNKNOWN(client_t *client, char *rest UNUSED) {
    return write_reply(client, 501, "Unknown SITE command.");
}

static int32_t ftp_SITE_LOAD(client_t *client, char *path UNUSED) {
    //   FILE *f = vrt_fopen(client->cwd, path, "rb");
    //   if (!f) return write_reply(client, 550, strerror(errno));
    //   char *real_path = to_real_path(client->cwd, path);
    //   if (!real_path) goto end;
    //   load_from_file(f, real_path);
    //   free(real_path);
    //   end:
    //   fclose(f);
    return write_reply(client, 500, "Unable to load.");
}

typedef int32_t (*ftp_command_handler)(client_t *client, char *args);

static int32_t dispatch_to_handler(client_t *client, char *cmd_line, const char **commands, const ftp_command_handler *handlers) {
    char cmd[FTP_BUFFER_SIZE], rest[FTP_BUFFER_SIZE];
    char *args[] = {cmd, rest};
    split(cmd_line, ' ', 1, args);
    int32_t i;
    for (i = 0; commands[i]; i++) {
        if (!strcasecmp(commands[i], cmd))
            break;
    }
    return handlers[i](client, rest);
}

static const char *site_commands[]               = {"LOADER", "CLEAR", "CHMOD", "PASSWD", "NOPASSWD", "EJECT", "MOUNT", "UNMOUNT", "LOAD", NULL};
static const ftp_command_handler site_handlers[] = {ftp_SITE_LOADER, ftp_SITE_CLEAR, ftp_SITE_CHMOD, ftp_SITE_PASSWD, ftp_SITE_NOPASSWD, ftp_SITE_EJECT, ftp_SITE_MOUNT, ftp_SITE_UNMOUNT,
                                                    ftp_SITE_LOAD, ftp_SITE_UNKNOWN};

static int32_t ftp_SITE(client_t *client, char *cmd_line) {
    return dispatch_to_handler(client, cmd_line, site_commands, site_handlers);
}

static int32_t ftp_NOOP(client_t *client, char *rest UNUSED) {
    return write_reply(client, 200, "NOOP command successful.");
}

static int32_t ftp_SUPERFLUOUS(client_t *client, char *rest UNUSED) {
    return write_reply(client, 202, "Command not implemented, superfluous at this site.");
}

static int32_t ftp_NEEDAUTH(client_t *client, char *rest UNUSED) {
    char msg[FTP_BUFFER_SIZE + 50] = "";
    sprintf(msg, "C[%d] Please login with USER and PASS", client->index + 1);
    return write_reply(client, 530, msg);
}

static int32_t ftp_UNKNOWN(client_t *client, char *rest UNUSED) {
    return write_reply(client, 502, "Command not implemented.");
}

static const char *unauthenticated_commands[]               = {"USER", "PASS", "QUIT", "REIN", "FEAT", "OPTS", "NOOP", NULL};
static const ftp_command_handler unauthenticated_handlers[] = {ftp_USER, ftp_PASS, ftp_QUIT, ftp_REIN, ftp_FEAT, ftp_OPTS, ftp_NOOP, ftp_NEEDAUTH};

static const char *authenticated_commands[] = {
        "USER", "PASS", "LIST", "PWD", "CWD", "CDUP",
        "SIZE", "PASV", "PORT", "TYPE", "SYST", "MODE",
        "RETR", "STOR", "APPE", "REST", "DELE", "MKD",
        "RMD", "RNFR", "RNTO", "NLST", "QUIT", "REIN",
        "SITE", "FEAT", "OPTS", "NOOP", "ALLO", NULL};
static const ftp_command_handler authenticated_handlers[] = {
        ftp_USER, ftp_PASS, ftp_LIST, ftp_PWD, ftp_CWD, ftp_CDUP,
        ftp_SIZE, ftp_PASV, ftp_PORT, ftp_TYPE, ftp_SYST, ftp_MODE,
        ftp_RETR, ftp_STOR, ftp_APPE, ftp_REST, ftp_DELE, ftp_MKD,
        ftp_DELE, ftp_RNFR, ftp_RNTO, ftp_NLST, ftp_QUIT, ftp_REIN,
        ftp_SITE, ftp_FEAT, ftp_OPTS, ftp_NOOP, ftp_SUPERFLUOUS,
        ftp_UNKNOWN};

/*
	returns negative to signal an error that requires closing the connection
*/
static int32_t process_command(client_t *client, char *cmd_line) {
    if (strlen(cmd_line) == 0) {
        return 0;
    }

    console_printf("Got command: %s\n", cmd_line);

    const char **commands               = unauthenticated_commands;
    const ftp_command_handler *handlers = unauthenticated_handlers;

    if (client->authenticated) {
        commands = authenticated_commands;
        handlers = authenticated_handlers;
    }

    return dispatch_to_handler(client, cmd_line, commands, handlers);
}

static void cleanup_data_resources(client_t *client) {

    // close the data_socket
    if (client->data_socket >= 0 && client->data_socket != client->passive_socket) {
        network_close_blocking(client->data_socket);
    }

    // transfer call back
    if (client->data_connection_cleanup) {
        client->data_connection_cleanup(client->data_connection_callback_arg);
    }

    resetClient(client);
}


static void displayTransferSpeedStats() {
    if (nbSpeedMeasures != 0) {
        console_printf(" ");
        console_printf("============================================================");
        console_printf("  Speed (MB/s) [min = %.2f, mean = %.2f, max = %.2f]", minTransferRate, sumAvgSpeed / (float) nbSpeedMeasures, maxTransferRate);
        console_printf("  Files received : %d / sent : %d", nbFilesReceived, nbFilesSent);
        console_printf("  Time ellapsed = %" PRIu64 " sec", (OSGetTime() - startTime) * 4000ULL / BUS_SPEED);
        console_printf("============================================================");

        OSSleepTicks(OSSecondsToTicks(2));
    }
}

static void cleanup_client(client_t *client) {
    network_close_blocking(client->socket);
    if (client->data_socket >= 0) cleanup_data_resources(client);
    console_printf("Client %d disconnected.\n", client->index + 1);

    close_passive_socket(client);
    initClient(client);
}

void cleanup_ftp() {
    int client_index;
    for (client_index = 0; client_index < MAX_CLIENTS; client_index++) {

        write_reply(clients[client_index], 421, "Service not available, closing control connection.");

        if (!OSIsThreadTerminated(clients[client_index]->transferThread)) {
            console_printf("C[%d] transfer of %s aborted !", clients[client_index]->index + 1, clients[client_index]->fileName);

            OSCancelThread(clients[client_index]->transferThread);
            OSTestThreadCancel();
        }

        cleanup_client(clients[client_index]);

        if (clients[client_index]->transferThread != NULL) free(clients[client_index]->transferThread);
        if (clients[client_index]->transferBuffer != NULL) free(clients[client_index]->transferBuffer);

        free(clients[client_index]);
    }
}

static bool process_accept_events(int32_t server) {
    int32_t peer;
    struct sockaddr_in client_address;
    socklen_t addrlen = sizeof(client_address);
    while ((peer = network_accept(server, (struct sockaddr *) &client_address, &addrlen)) != -EAGAIN) {
        if (peer < 0) {
            console_printf("Error accepting connection: [%i] %s\n", -peer, strerror(-peer));
            return false;
        }

        // search for the first available connections (socket == -1)
        int client_index;
        for (client_index = 0; client_index < MAX_CLIENTS; client_index++) {

            if (clients[client_index]->socket == -1) {

                // use this connection C[client_index]
                clients[client_index]->socket = peer;
                memcpy(&clients[client_index]->address, &client_address, sizeof(client_address));

                char msg[FTPMAXPATHLEN] = "";
                if ((client_index == 0) && (nbFilesSent == 0) && (nbFilesReceived == 0)) {
                    // send recommendations to client
                    sprintf(msg, "ftpiiu : %d connections max (%d simulatneous transfers), %d sec for timeout", MAX_CLIENTS, MAX_CLIENTS - 1, FTP_CLIENT_CONNECTION_TIMEOUT);
                } else {
                    // send recommendations send stats to client

                    // compute end time
                    uint64_t duration = (OSGetTime() - startTime) * 4000ULL / BUS_SPEED;

                    sprintf(msg, "Transfers in MB/s: min=%.2f,  mean=%.2f,  max=%.2f | received:%d / sent:%d | time ellapsed %" PRIu64 "s", minTransferRate, nbSpeedMeasures == 0 ? 0.0 : sumAvgSpeed / (float) nbSpeedMeasures, maxTransferRate, nbFilesReceived, nbFilesSent, duration / 1000);
                }

                if (write_reply(clients[client_index], 220, msg) < 0) {
                    network_close_blocking(peer);
                    clients[client_index]->socket = -1;
                    return false;
                } else {
                    console_printf("C[%d] connected to %s!\n", client_index + 1, inet_ntoa(client_address.sin_addr));
                    clients[client_index]->index = client_index;
                    return true;
                }
            }
        }
        // here, max of connections are in use
        console_printf("Maximum of %u clients reached, not accepting client.\n", MAX_CLIENTS);
    }
    return true;
}

static void process_data_events(client_t *client) {
    int32_t result = 0;
    if (!client->data_connection_connected) {
        if (client->passive_socket >= 0) {
            struct sockaddr_in data_peer_address;
            socklen_t addrlen = sizeof(data_peer_address);
            result            = network_accept(client->passive_socket, (struct sockaddr *) &data_peer_address, &addrlen);
            if (result >= 0) {
                client->data_socket               = result;
                client->data_connection_connected = true;
                // exit function
                return;
            } else {
                if (result != -EAGAIN) {
                    char msg[FTP_BUFFER_SIZE] = "";
                    sprintf(msg, "Error accepting C[%d] %d (%s)", client->index + 1, errno, strerror(errno));
                    write_reply(client, 550, msg);
                }
            }
        } else {
            // retry if can't connect before exiting
            int nbTries = 0;
        try_again:
            if ((result = network_connect(client->data_socket, (struct sockaddr *) &client->address, sizeof(client->address))) < 0) {
                if (result == -EINPROGRESS || result == -EALREADY) {
                    nbTries++;
                    OSSleepTicks(OSMillisecondsToTicks(MAX_CLIENTS * 20));

                    if (nbTries <= FTP_RETRIES_NUMBER) goto try_again;
                    // no need to set to -EAGAIN, exit
                    return;
                }
                if ((result != -EAGAIN) && (result != -EISCONN)) {
                    console_printf("! ERROR : C[%d] unable to connect to client: rc=%d, err=%s", client->index + 1, -result, strerror(-result));
                }
            }
            if (result >= 0 || result == -EISCONN) {
                client->data_connection_connected = true;
                client->speed                     = 0;
                if (result >= 0) return;
            }
        }

        if (OSGetTime() > client->data_connection_timer) {

            char msg[FTPMAXPATHLEN + 40] = "";
            sprintf(msg, "C[%d] timed out when connecting", client->index + 1);
            write_reply(client, 425, msg);

            console_printf("%s.\n", msg);
            if (client->data_socket != -1) cleanup_data_resources(client);
            else
                cleanup_client(client);

            return;
        }

    } else {
        result = client->data_callback(client->data_socket, client->data_connection_callback_arg);
        // file transfer finished
        if (client->transferCallback == 0 && client->bytesTransferred > 0) {

            // compute transfer speed
            uint64_t duration = (OSGetTime() - (client->data_connection_timer - (OSTime) (FTP_SERVER_CONNECTION_TIMEOUT) *1000000000)) * 4000ULL / BUS_SPEED;
            if (duration != 0) {

                // set a threshold on file size to consider file for average calculation
                // take only files larger than the network buffer used

                if (client->bytesTransferred >= 2 * DEFAULT_NET_BUFFER_SIZE) {
                    client->speed = (float) (client->bytesTransferred) / (float) (duration * 1000);

                    if (strlen(client->uploadFilePath) == 0) {
                        console_printf("> C[%d] %s sent at %.2f MB/s (%d bytes)", client->index + 1, client->fileName, client->speed, client->bytesTransferred);
                    } else {
                        console_printf("> C[%d] %s received at %.2f MB/s (%d bytes)", client->index + 1, client->fileName, client->speed, client->bytesTransferred);
                    }
                }
            }
        }

        if (result < 0 && result != -EAGAIN) {
            console_printf("! ERROR : C[%d] data transfer callback using socket %d failed , socket error = %d", client->index + 1, client->data_socket, result);
        }
    }


    if (result == -EAGAIN) return;

    if (result <= 0) {

        if (result < 0) {
            if (result != -2) {
                char msg[FTPMAXPATHLEN] = "";
                sprintf(msg, "C[%d] to be closed : error = %d (%s)", client->index + 1, result, strerror(result));
                write_reply(client, 520, msg);
            }
            cleanup_client(client);
        } else {

            char msg[FTPMAXPATHLEN + 80] = "";
            if (client->transferCallback == 0) {
                if (client->speed != 0)
                    sprintf(msg, "C[%d] %s transferred successfully %.0fKB/s", client->index + 1, client->fileName, client->speed * 1000);
                else
                    sprintf(msg, "C[%d] %s transferred successfully", client->index + 1, client->fileName);
                result = 0;
            } else
                sprintf(msg, "C[%d] command executed successfully", client->index + 1);

            write_reply(client, 226, msg);

            if (result == 0) {
                cleanup_data_resources(client);
            }
        }
    }
}

static void process_control_events(client_t *client) {
    int32_t bytes_read;
    while (client->offset < (FTP_BUFFER_SIZE - 1)) {
        if (client->data_callback) {
            return;
        }
        char *offset_buf = client->buf + client->offset;
        if ((bytes_read = network_read(client->socket, offset_buf, FTP_BUFFER_SIZE - 1 - client->offset)) < 0) {
            if (bytes_read != -EAGAIN) {
                console_printf("C[%d] read error %i occurred, closing client.\n", client->index + 1, bytes_read);
                goto recv_loop_end;
            }
            return;
        } else if (bytes_read == 0) {
            goto recv_loop_end; // EOF from client
        }
        client->offset += bytes_read;
        client->buf[client->offset] = '\0';

        if (strchr(offset_buf, '\0') != (client->buf + client->offset)) {
            console_printf("C[%d] received a null byte from client, closing connection ;-)\n", client->index + 1); // i have decided this isn't allowed =P
            goto recv_loop_end;
        }

        char *next;
        char *end;
        for (next = client->buf; (end = strstr(next, CRLF)) && !client->data_callback; next = end + CRLF_LENGTH) {
            *end = '\0';
            if (strchr(next, '\n')) {
                console_printf("C[%d] received a line-feed from client without preceding carriage return, closing connection ;-)\n", client->index + 1); // i have decided this isn't allowed =P
                goto recv_loop_end;
            }

            if (*next) {
                int32_t result = 0;
                if ((result = process_command(client, next)) < 0) {
                    if (result != -EQUIT) {
                        console_printf("Closing connection due to error while processing command: %s\n", next);
                    }
                    goto recv_loop_end;
                }
            }
        }

        if (next != client->buf) { // some lines were processed
            client->offset = strlen(next);
            char tmp_buf[client->offset];
            memcpy(tmp_buf, next, client->offset);
            memcpy(client->buf, tmp_buf, client->offset);
        }
    }
    console_printf("Received line longer than %u bytes, closing client.\n", FTP_BUFFER_SIZE - 1);

recv_loop_end:
    cleanup_client(client);
}

bool process_ftp_events(int32_t server) {
    bool network_down = !process_accept_events(server);
    if (!network_down) {
        int client_index;
        float totalSpeedMBs     = 0;
        uint8_t nbActiveClients = 0;

        // Treat all actives clients/connections
        for (client_index = 0; client_index < MAX_CLIENTS; client_index++) {
            // if client is connected
            if (clients[client_index]->socket != -1) {
                nbActiveClients++;
                if (clients[client_index]->data_callback) {
                    // total is the sum of speed computed for each connection
                    if (clients[client_index]->speed) totalSpeedMBs += clients[client_index]->speed;

                    process_data_events(clients[client_index]);
                } else {
                    process_control_events(clients[client_index]);
                }
            }
        }

        if (totalSpeedMBs > 0) {
            // increment nbSpeedMeasures and take speed into account for mean calculation
            nbSpeedMeasures++;

            // fix mutli rate errors
            int nbt = (int) (totalSpeedMBs / maxSpeedPerTransfer);
            if (nbt > 1) totalSpeedMBs = totalSpeedMBs / nbt;

            if (totalSpeedMBs > maxTransferRate) maxTransferRate = totalSpeedMBs;
            if (totalSpeedMBs < minTransferRate) minTransferRate = totalSpeedMBs;
            sumAvgSpeed += totalSpeedMBs;

        } else {

            // no transfer is running
            if (nbActiveClients == 1 && lastSumAvgSpeed != sumAvgSpeed) {
                // last stats saved are outdated
                displayTransferSpeedStats();
                // save last stats
                lastSumAvgSpeed = sumAvgSpeed;
            }
        }

        if (nbActiveClients == 0) {
            // sleep 2 sec before listening again
            OSSleepTicks(OSSecondsToTicks(2));
        } else {
            // let the server breathe
            OSSleepTicks(OSMillisecondsToTicks(125));
        }
    }

    return network_down;
}
