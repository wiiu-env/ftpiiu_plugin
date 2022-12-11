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
#ifndef _FTP_H_
#define _FTP_H_

#include <coreinit/thread.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>

// to avoid warnings
#define UNUSED                        __attribute__((unused))

// network allowed 8 simultaneuous connections max (before  getting errors)
// 8 transfers + 1 browsing connection = 9
#define MAX_CLIENTS                   9

// Connection time out in seconds
// set timeout to 50sec (server can take more than 40 sec to respond when you drag many files from client)
#define FTP_SERVER_CONNECTION_TIMEOUT 50
#define FTP_CLIENT_CONNECTION_TIMEOUT FTP_SERVER_CONNECTION_TIMEOUT + 10

// number of reties on socket operations
#define FTP_RETRIES_NUMBER            MAX_CLIENTS * 2

// size of the message sent to clients
#define FTP_BUFFER_SIZE               1024

// max length for string (whole line send to client must be lower than FTP_BUFFER_SIZE)
#define FTPMAXPATHLEN                 256

// default socket buffer size (max of value than can be set with setsockopt on SND/RCV buffers)
#define DEFAULT_NET_BUFFER_SIZE       (128 * 1024)

// stack size of each transfer thread
#define FTP_TRANSFER_STACK_SIZE       0x2000

#ifdef __cplusplus
extern "C" {
#endif

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
    // index of the connection
    uint32_t index;
    // file to transfer
    FILE *f;
    // name of the file to upload
    char fileName[FTPMAXPATHLEN];
    // path to the file to upload
    char uploadFilePath[FTPMAXPATHLEN];
    // thread for transfering
    OSThread *transferThread;
    // preallocated transfer thread stack
    uint8_t transferThreadStack[FTP_TRANSFER_STACK_SIZE];
    // buffer for transferring files
    char *transferBuffer;
    // for data transfer tracking
    int32_t bytesTransferred;
    // last speed computed in MB/s
    float speed;
    // return code of send/recv functions
    // when transferring = amount of data read/sent on each network operation
    int32_t transferCallback;
};

typedef struct client_struct client_t;

int32_t create_server(uint16_t port);

void accept_ftp_client(int32_t server);

void set_ftp_password(char *new_password);

bool process_ftp_events(int32_t server);

void cleanup_ftp();

#ifdef __cplusplus
}
#endif

#endif /* _FTP_H_ */
