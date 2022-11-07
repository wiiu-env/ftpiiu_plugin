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
#include "main.h"
#include "utils/logger.h"
#include <coreinit/thread.h>
#include <errno.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dir.h>
#include <sys/param.h>
#include <unistd.h>

//! TODO: fix those function
#define gettime() OSGetTick()

#include "ftp.h"
#include "net.h"
#include "virtualpath.h"
#include "vrt.h"

#define UNUSED          __attribute__((unused))

// size of the message sent to clients
#define FTP_BUFFER_SIZE 1024

// network allowed 8 simultaneuous connections (max value before  getting errors)
// 8 transfers + 1 browsing connection = 9
#define MAX_CLIENTS     9

static const uint16_t SRC_PORT    = 20;
static const int32_t EQUIT        = 696969;
static const char *CRLF           = "\r\n";
static const uint32_t CRLF_LENGTH = 2;

static uint8_t num_clients   = 0;
static uint16_t passive_port = 1024;
static char *password        = NULL;

// priority of curent thread (when starting server)
static int32_t mainPriority = 16;

#define console_printf(FMT, ARGS...) DEBUG_FUNCTION_LINE_WRITE(FMT, ##ARGS);

#define SOCKET_MOPT_STACK_SIZE 0x2000
extern int somemopt (int req_type, char* mem, unsigned int memlen, int flags);

// thread for socket memory optimization
static WUT_ALIGNAS(32) OSThread socketOptThread;
static uint8_t WUT_ALIGNAS(8) *socketOptThreadStack=NULL;


typedef int32_t (*data_connection_callback)(int32_t data_socket, void *arg);

struct client_struct {
    int32_t socket;
    char representation_type;
    int32_t passive_socket;
    int32_t data_socket;
    char cwd[FTPMAXPATHLEN];
    char pending_rename[FTPMAXPATHLEN];
    off_t restart_marker;
    struct sockaddr_in address;
    bool authenticated;
    char buf[FTP_BUFFER_SIZE];
    int32_t offset;
    bool data_connection_connected;
    data_connection_callback data_callback;
    void *data_connection_callback_arg;

    void (*data_connection_cleanup)(void *arg);

    uint64_t data_connection_timer;
};

typedef struct client_struct client_t;

static client_t WUT_ALIGNAS(64) *clients[MAX_CLIENTS] = {NULL};

// socket memory optimization
// somemopt() will block until socket_lib_finish() call, so launch it in a separate
// thread
int socketOptThreadMain(int argc UNUSED, const char **argv UNUSED)
{
    // allocate the buffer for socket memory optimization 
    // size = DEFAULT_NET_BUFFER_SIZE * 4 (read/write with double buffering)
	int bufSize = 4*DEFAULT_NET_BUFFER_SIZE;
	
	void WUT_ALIGNAS(64) *smoBuf = NULL;
    smoBuf = (void *) memalign(64, bufSize);
    if (somemopt(0x01, smoBuf, bufSize, 0) == -1 && errno != 50) {
		console_printf("! ERROR : somemopt failed !");
	}

    free(smoBuf);

    return 0;
}

int32_t create_server(uint16_t port) {

    // get the current thread (on CPU1)
    OSThread *thread = NULL;
    thread = OSGetCurrentThread();
    if (thread != NULL) {
        // get priority of current thread
        mainPriority = OSGetThreadPriority(thread);
    }
	
	// set socket memory optimization
    socketOptThreadStack = (uint8_t *) memalign(8, SOCKET_MOPT_STACK_SIZE);
    
    // set priority to mainPiority and launch on CPU0
    if (socketOptThreadStack == NULL || !OSCreateThread(&socketOptThread, socketOptThreadMain, 0, NULL, socketOptThreadStack + SOCKET_MOPT_STACK_SIZE, SOCKET_MOPT_STACK_SIZE, mainPriority, OS_THREAD_ATTRIB_AFFINITY_CPU0)) {
        console_printf("! ERROR : failed to create socket memory optimization thread!");
        return -ENOMEM;
    }
    OSSetThreadName(&socketOptThread, "Socket memory optimizer thread");
    OSResumeThread(&socketOptThread);

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
        network_close(server);
        //gxprintf("Error binding socket: [%i] %s\n", -ret, strerror(-ret));
        return ret;
    }
    if ((ret = network_listen(server, MAX_CLIENTS)) < 0) {
        network_close(server);
        //gxprintf("Error listening on socket: [%i] %s\n", -ret, strerror(-ret));
        return ret;
    }

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
    char msg[15];
    snprintf(msg, sizeof(msg), "Type set to %s.", representation_type);
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

static int32_t ftp_PWD(client_t *client, char *rest UNUSED) {
    char msg[FTPMAXPATHLEN + 24];
    // TODO: escape double-quotes
    sprintf(msg, "\"%s\" is current directory.", client->cwd);
    return write_reply(client, 257, msg);
}

static int32_t ftp_CWD(client_t *client, char *path) {
    int32_t result;
    if (!vrt_chdir(client->cwd, path)) {
        result = write_reply(client, 250, "CWD command successful.");
    } else {
        result = write_reply(client, 550, strerror(errno));
    }
    return result;
}

static int32_t ftp_CDUP(client_t *client, char *rest UNUSED) {
    int32_t result;
    if (!vrt_chdir(client->cwd, "..")) {
        result = write_reply(client, 250, "CDUP command successful.");
    } else {
        result = write_reply(client, 550, strerror(errno));
    }
    return result;
}

static int32_t ftp_DELE(client_t *client, char *path) {
    if (!vrt_unlink(client->cwd, path)) {
        return write_reply(client, 250, "File or directory removed.");
    } else {
        return write_reply(client, 550, strerror(errno));
    }
}

static int32_t ftp_MKD(client_t *client, char *path) {
    if (!*path) {
        return write_reply(client, 501, "Syntax error in parameters.");
    }
    if (!vrt_mkdir(client->cwd, path, 0777)) {
        char msg[FTPMAXPATHLEN + 21];
        char abspath[FTPMAXPATHLEN];
        strcpy(abspath, client->cwd);
        vrt_chdir(abspath, path); // TODO: error checking
        // TODO: escape double-quotes
        sprintf(msg, "\"%s\" directory created.", abspath);
        return write_reply(client, 257, msg);
    } else {
        return write_reply(client, 550, strerror(errno));
    }
}

static int32_t ftp_RNFR(client_t *client, char *path) {
    strcpy(client->pending_rename, path);
    return write_reply(client, 350, "Ready for RNTO.");
}

static int32_t ftp_RNTO(client_t *client, char *path) {
    if (!*client->pending_rename) {
        return write_reply(client, 503, "RNFR required first.");
    }
    int32_t result;
    if (!vrt_rename(client->cwd, client->pending_rename, path)) {
        result = write_reply(client, 250, "Rename successful.");
    } else {
        result = write_reply(client, 550, strerror(

                                                  errno));
    }
    *client->pending_rename = '\0';
    return result;
}

static int32_t ftp_SIZE(client_t *client, char *path) {
    struct stat st;
    if (!vrt_stat(client->cwd, path, &st)) {
        char size_buf[12];
        sprintf(size_buf, "%llu", st.st_size);
        return write_reply(client, 213, size_buf);
    } else {
        return write_reply(client, 550, strerror(errno));
    }
}

static int32_t ftp_PASV(client_t *client, char *rest UNUSED) {
    close_passive_socket(client);

    int32_t result;
    struct sockaddr_in bindAddress;
    while (passive_port < 5000) {
        client->passive_socket = network_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (client->passive_socket < 0) {
            return write_reply(client, 520, "Unable to create listening socket.");
        }
        set_blocking(client->passive_socket, false);

        memset(&bindAddress, 0, sizeof(bindAddress));
        bindAddress.sin_family      = AF_INET;
        bindAddress.sin_port        = htons(passive_port); // XXX: BUG: This will overflow eventually, with interesting results...
        bindAddress.sin_addr.s_addr = htonl(INADDR_ANY);

        passive_port++;

        if ((result = network_bind(client->passive_socket, (struct sockaddr *) &bindAddress, sizeof(bindAddress))) >= 0) {
            break;
        } else {
            close_passive_socket(client);
        }
    }
    if (passive_port >= 5000) {
        passive_port = 1024;
    }
    if (result < 0) {
        close_passive_socket(client);
        return write_reply(client, 520, "Unable to bind listening socket.");
    }
    if ((result = network_listen(client->passive_socket, 1)) < 0) {
        close_passive_socket(client);
        return write_reply(client, 520, "Unable to listen on socket.");
    }
    char reply[49];
    uint16_t port       = bindAddress.sin_port;
    uint32_t ip         = network_gethostip();
    struct in_addr addr = {};
    addr.s_addr         = ip;
    console_printf("Listening for data connections at %s:%u...\n", inet_ntoa(addr), port);
    sprintf(reply, "Entering Passive Mode (%u,%u,%u,%u,%u,%u).", (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff, (port >> 8) & 0xff, port & 0xff);
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
    int32_t data_socket = network_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (data_socket < 0)
        return data_socket;
    set_blocking(data_socket, false);
    struct sockaddr_in bindAddress;
    memset(&bindAddress, 0, sizeof(bindAddress));
    bindAddress.sin_family      = AF_INET;
    bindAddress.sin_port        = htons(SRC_PORT);
    bindAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    int32_t result;
    if ((result = network_bind(data_socket, (struct sockaddr *) &bindAddress, sizeof(bindAddress))) < 0) {
        network_close(data_socket);
        return result;
    }

    client->data_socket = data_socket;
    console_printf("Attempting to connect to client at %s:%u\n", inet_ntoa(client->address.sin_addr), client->address.sin_port);
    return 0;
}

static int32_t prepare_data_connection_passive(client_t *client, data_connection_callback callback UNUSED, void *arg UNUSED) {
    client->data_socket = client->passive_socket;
    console_printf("Waiting for data connections...\n");
    return 0;
}

static int32_t prepare_data_connection(client_t *client, void *callback, void *arg, void *cleanup) {
    int32_t result = write_reply(client, 150, "Transferring data.");
    if (result >= 0) {
        data_connection_handler handler = prepare_data_connection_active;
        if (client->passive_socket >= 0)
            handler = prepare_data_connection_passive;
        result = handler(client, (data_connection_callback) callback, arg);
        if (result < 0) {
            result = write_reply(client, 520, "Closing data connection, error occurred during transfer.");
        } else {
            client->data_connection_connected    = false;
            client->data_callback                = callback;
            client->data_connection_callback_arg = arg;
            client->data_connection_cleanup      = cleanup;
            client->data_connection_timer        = gettime() + OSSecondsToTicks(10);
        }
    }
    return result;
}

static int32_t send_nlst(int32_t data_socket, DIR_P *iter) {
    int32_t result = 0;
    char filename[FTPMAXPATHLEN];
    struct dirent *dirent = NULL;
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
    char filename[MAXPATHLEN];
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
        snprintf(line, sizeof(line), "%c%s%s%s%s%s%s%s%s%s	1 0		0	 %10llu %s %s\r\n", (dirent->d_type & DT_DIR) ? 'd' : '-',
                 st.st_mode & S_IRUSR ? "r" : "-",
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
        return write_reply(client, 550, strerror(errno));
    }

    int32_t result = prepare_data_connection(client, send_nlst, dir, vrt_closedir);
    if (result < 0)
        vrt_closedir(dir);
    return result;
}

static int32_t ftp_LIST(client_t *client, char *path) {
    if (*path == '-') {
        // handle buggy clients that use "LIST -aL" or similar, at the expense of breaking paths that begin with '-'
        char flags[FTP_BUFFER_SIZE];
        char rest[FTP_BUFFER_SIZE];
        char *args[] = {flags, rest};
        split(path, ' ', 1, args);
        path = rest;
    }
    if (!*path) {
        path = ".";
    }

    if (path && client->cwd) {
        if (strcmp(path, ".") == 0 && strcmp(client->cwd, "/") == 0) {
            UnmountVirtualPaths();
            MountVirtualDevices();
        }
    }

    DIR_P *dir = vrt_opendir(client->cwd, path);
    if (dir == NULL) {
        return write_reply(client, 550, strerror(errno));
    }

    int32_t result = prepare_data_connection(client, send_list, dir, vrt_closedir);
    if (result < 0)
        vrt_closedir(dir);
    return result;
}

static int32_t ftp_RETR(client_t *client, char *path) {
    FILE *f = vrt_fopen(client->cwd, path, "rb");
    if (!f) {
        return write_reply(client, 550, strerror(errno));
    }

    int fd = fileno(f);
    if (client->restart_marker && lseek(fd, client->restart_marker, SEEK_SET) != client->restart_marker) {
        int32_t lseek_error = errno;
        fclose(f);
        client->restart_marker = 0;
        return write_reply(client, 550, strerror(lseek_error));
    }
    client->restart_marker = 0;

    int32_t result = prepare_data_connection(client, send_from_file, f, fclose);
    if (result < 0)
        fclose(f);
    return result;
}

static int32_t stor_or_append(client_t *client, FILE *f) {
    if (!f) {
        return write_reply(client, 550, strerror(errno));
    }
    int32_t result = prepare_data_connection(client, recv_to_file, f, fclose);
    if (result < 0)
        fclose(f);
    return result;
}

static int32_t ftp_STOR(client_t *client, char *path) {
    char *openMode = "wb";
    if (client->restart_marker) {
        openMode = "r+";
    }
    FILE *f = vrt_fopen(client->cwd, path, openMode);
    int fd;
    if (f) {
        fd = fileno(f);
    }
    if (f && client->restart_marker && lseek(fd, client->restart_marker, SEEK_SET) != client->restart_marker) {
        int32_t lseek_error = errno;
        fclose(f);
        client->restart_marker = 0;
        return write_reply(client, 550, strerror(lseek_error));
    }
    client->restart_marker = 0;

    return stor_or_append(client, f);
}

static int32_t ftp_APPE(client_t *client, char *path) {
    return stor_or_append(client, vrt_fopen(client->cwd, path, "ab"));
}

static int32_t ftp_REST(client_t *client, char *offset_str) {
    off_t offset;
    if (sscanf(offset_str, "%lli", &offset) < 1 || offset < 0) {
        return write_reply(client, 501, "Syntax error in parameters.");
    }
    client->restart_marker = offset;
    char msg[FTP_BUFFER_SIZE];
    sprintf(msg, "Restart position accepted (%lli).", offset);
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
    return write_reply(client, 250, "SITE CHMOD command ok.");
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
    return write_reply(client, 530, "Please login with USER and PASS.");
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
    if (client->data_socket >= 0 && client->data_socket != client->passive_socket) {
        network_close_blocking(client->data_socket);
    }
    client->data_socket               = -1;
    client->data_connection_connected = false;
    client->data_callback             = NULL;
    if (client->data_connection_cleanup) {
        client->data_connection_cleanup(client->data_connection_callback_arg);
    }
    client->data_connection_callback_arg = NULL;
    client->data_connection_cleanup      = NULL;
    client->data_connection_timer        = 0;
}

static void cleanup_client(client_t *client) {
    network_close_blocking(client->socket);
    cleanup_data_resources(client);
    close_passive_socket(client);
    int client_index;
    for (client_index = 0; client_index < MAX_CLIENTS; client_index++) {
        if (clients[client_index] == client) {
            clients[client_index] = NULL;
            break;
        }
    }
    free(client);
    client = NULL;
    num_clients--;
    console_printf("Client disconnected.\n");
}

void cleanup_ftp() {
    int client_index;
    for (client_index = 0; client_index < MAX_CLIENTS; client_index++) {
        client_t *client = clients[client_index];
        if (client) {
            write_reply(client, 421, "Service not available, closing control connection.");
            cleanup_client(client);
        }
    }
    int ret;
    OSJoinThread(&socketOptThread, &ret);
    
    if (socketOptThreadStack != NULL) free(socketOptThreadStack);
}

static bool process_accept_events(int32_t server) {
    // if the max connections number is not reached, treat incomming connections
    if (num_clients < MAX_CLIENTS) {
	    int32_t peer;
	    struct sockaddr_in client_address;
	    socklen_t addrlen = sizeof(client_address);
	    while ((peer = network_accept(server, (struct sockaddr *) &client_address, &addrlen)) != -EAGAIN) {
	        if (peer < 0) {
	            console_printf("Error accepting connection: [%i] %s\n", -peer, strerror(-peer));
	            return false;
	        }

	        console_printf("Accepted connection from %s!\n", inet_ntoa(client_address.sin_addr));

		        if (num_clients == MAX_CLIENTS-1) {
	            console_printf("Maximum of %u clients reached, not accepting client.\n", MAX_CLIENTS);
	            network_close(peer);
	            return true;
	        }

		    client_t *client = (client_t *) memalign(64, sizeof(client_t));
	        if (!client) {
	            console_printf("Could not allocate memory for client state, not accepting client.\n");
	            network_close(peer);
	            return true;
	        }
	        client->socket              = peer;
	        client->representation_type = 'A';
	        client->passive_socket      = -1;
	        client->data_socket         = -1;
	        strcpy(client->cwd, "/");
	        *client->pending_rename              = '\0';
	        client->restart_marker               = 0;
	        client->authenticated                = false;
	        client->offset                       = 0;
	        client->data_connection_connected    = false;
	        client->data_callback                = NULL;
	        client->data_connection_callback_arg = NULL;
	        client->data_connection_cleanup      = NULL;
	        client->data_connection_timer        = 0;
	        memcpy(&client->address, &client_address, sizeof(client_address));
	        int client_index;
	        if (write_reply(client, 220, "ftpii") < 0) {
	            console_printf("Error writing greeting.\n");
	            network_close_blocking(peer);
	            free(client);
	            client = NULL;
	        } else {
	            for (client_index = 0; client_index < MAX_CLIENTS; client_index++) {
	                if (!clients[client_index]) {
	                    clients[client_index] = client;
	                    break;
	                }
	            }
	            num_clients++;
	        }
	    }
	}
    return true;
}

static void process_data_events(client_t *client) {
    int32_t result;
    if (!client->data_connection_connected) {
        if (client->passive_socket >= 0) {
            struct sockaddr_in data_peer_address;
            socklen_t addrlen = sizeof(data_peer_address);
            result            = network_accept(client->passive_socket, (struct sockaddr *) &data_peer_address, &addrlen);
            if (result >= 0) {
                client->data_socket               = result;
                client->data_connection_connected = true;
            }
        } else {
            if ((result = network_connect(client->data_socket, (struct sockaddr *) &client->address, sizeof(client->address))) < 0) {
                if (result == -EINPROGRESS || result == -EALREADY)
                    result = -EAGAIN;
                if ((result != -EAGAIN) && (result != -EISCONN)) {
                    console_printf("Unable to connect to client: [%i] %s\n", -result, strerror(-result));
                }
            }
            if (result >= 0 || result == -EISCONN) {
                client->data_connection_connected = true;
            }
        }
        if (client->data_connection_connected) {
            result = 1;
            console_printf("Connected to client!  Transferring data...\n");
        } else if (gettime() > client->data_connection_timer) {
            result = -2;
            console_printf("Timed out waiting for data connection.\n");
        }
    } else {
        result = client->data_callback(client->data_socket, client->data_connection_callback_arg);
    }

    if (result <= 0 && result != -EAGAIN) {
        cleanup_data_resources(client);
        if (result < 0) {
            result = write_reply(client, 520, "Closing data connection, error occurred during transfer.");
        } else {
            result = write_reply(client, 226, "Closing data connection, transfer successful.");
        }
        if (result < 0) {
            cleanup_client(client);
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
                console_printf("Read error %i occurred, closing client.\n", bytes_read);
                goto recv_loop_end;
            }
            return;
        } else if (bytes_read == 0) {
            goto recv_loop_end; // EOF from client
        }
        client->offset += bytes_read;
        client->buf[client->offset] = '\0';

        if (strchr(offset_buf, '\0') != (client->buf + client->offset)) {
            console_printf("Received a null byte from client, closing connection ;-)\n"); // i have decided this isn't allowed =P
            goto recv_loop_end;
        }

        char *next;
        char *end;
        for (next = client->buf; (end = strstr(next, CRLF)) && !client->data_callback; next = end + CRLF_LENGTH) {
            *end = '\0';
            if (strchr(next, '\n')) {
                console_printf("Received a line-feed from client without preceding carriage return, closing connection ;-)\n"); // i have decided this isn't allowed =P
                goto recv_loop_end;
            }

            if (*next) {
                int32_t result;
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
    int client_index;
    bool hasActiveClients = false;
    for (client_index = 0; client_index < MAX_CLIENTS; client_index++) {
        client_t *client = clients[client_index];
        if (client) {
            hasActiveClients = true;
            if (client->data_callback) {
                process_data_events(client);
            } else {
                process_control_events(client);
            }
        }
    }
    if (!hasActiveClients) {
        OSSleepTicks(OSMillisecondsToTicks(100));
    } else {
        OSSleepTicks(OSMillisecondsToTicks(1));
    }
    return network_down;
}
