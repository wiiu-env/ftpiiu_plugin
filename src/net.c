/*

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
#include <coreinit/thread.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/fcntl.h>
#include <unistd.h>

#define MIN(x, y) ((x) < (y) ? (x) : (y))

#include "net.h"

#define SO_RUSRBUF          0x10000 // enable userspace socket buffer

extern uint32_t hostIpAddress;

#if 0
void initialise_network() {
    printf("Waiting for network to initialise...\n");
    int32_t result = -1;
    while (!check_reset_synchronous() && result < 0) {
        net_deinit();
        while (!check_reset_synchronous() && (result = net_init()) == -EAGAIN);
        if (result < 0)
            printf("net_init() failed: [%i] %s, retrying...\n", result, strerror(-result));
    }
    if (result >= 0) {
        uint32_t ip = 0;
        do {
            ip = net_gethostip();
            if (!ip)
                printf("net_gethostip() failed, retrying...\n");
        } while (!check_reset_synchronous() && !ip);
        if (ip) {
            struct in_addr addr;
            addr.s_addr = ip;
            printf("Network initialised.  Wii IP address: %s\n", inet_ntoa(addr));
        }
    }
}
#endif

static bool retry(int32_t socketError) {
    bool status = false;

    // retry
    if (socketError == -EINPROGRESS ||
        socketError == -EALREADY ||
        socketError == -EBUSY ||
        socketError == -ETIME ||
        socketError == -ECONNREFUSED ||
        socketError == -ECONNABORTED ||
        socketError == -ETIMEDOUT ||
        socketError == -EMFILE ||
        socketError == -ENFILE ||
        socketError == -EHOSTUNREACH ||
        socketError == -EISCONN) status = true;

    return status;
}

int32_t network_socket(int32_t domain, int32_t type, int32_t protocol) {
    int sock = socket(domain, type, protocol);
    if (sock < 0) {
        int err = -errno;
        return (err < 0) ? err : sock;
    }

    uint32_t enable = 1;
    // reuse (avoid "Not enought Space" error when transferring a large numbers of files)
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    // Activate WinScale
    setsockopt(sock, SOL_SOCKET, SO_WINSCALE, &enable, sizeof(enable));

    // try socket memory optimization
	int retries = 0; 
    while (1) {
        if (setsockopt(sock, SOL_SOCKET, SO_RUSRBUF, &enable, sizeof(enable)) == 0)
            break;
		if (retries++ > FTP_RETRIES_NUMBER*4)
			break;			
        OSSleepTicks(OSMillisecondsToTicks(10));
    }
    
    // Set non blocking mode
    set_blocking(sock, false);

    return sock;
}

int32_t network_bind(int32_t s, struct sockaddr *name, int32_t namelen) {
    int res = bind(s, name, namelen);
    if (res < 0) {
        int err = -errno;
        return (err < 0) ? err : res;
    }
    return res;
}

int32_t network_listen(int32_t s, uint32_t backlog) {
    int res = listen(s, backlog);
    if (res < 0) {
        int err = -errno;
        return (err < 0) ? err : res;
    }
    return res;
}

int32_t network_accept(int32_t s, struct sockaddr *addr, socklen_t *addrlen) {
    int res = accept(s, addr, addrlen);
    if (res < 0) {
        int err = -errno;
        return (err < 0) ? err : res;
    }
    return res;
}

int32_t network_connect(int32_t s, struct sockaddr *addr, int32_t addrlen) {
    int res = connect(s, addr, addrlen);
    if (res < 0) {
        int err = -errno;
        return (err < 0) ? err : res;
    }
    return res;
}

int32_t network_read(int32_t s, void *mem, int32_t len) {
    int res = recv(s, mem, len, 0);
    if (res < 0) {
        int err = -errno;
        return (err < 0) ? err : res;
    }
    return res;
}

// read from network by chunk (len long)
static int32_t network_readChunk(int32_t s, void *mem, int32_t len) {

    int32_t received = 0;
    int ret          = -1;

    // while buffer is not full (len>0)
    while (len > 0) {
        // max ret value is 2*setsockopt value on SO_RCVBUF
        ret = recv(s, mem, len, 0);
        if (ret == 0) {
            // client EOF detected
            break;
        } else if (ret < 0 && errno != EAGAIN && errno != ENODATA) {
            int err  = -errno;
            received = (err < 0) ? err : ret;
            break;
        } else {
            if (ret > 0) {
                received += ret;
                len -= ret;
                mem += ret;
            }
        }
    }
    // here len could be < 0 and so more than len bytes are read
    // received > len and mem up to date
    return received;
}

uint32_t network_gethostip() {
    return hostIpAddress;
}

int32_t network_write(int32_t s, const void *mem, int32_t len) {
    int32_t transferred = 0;

    while (len) {
        int ret = send(s, mem, len, 0);
        if (ret < 0 && errno != EAGAIN && errno != ENODATA) {
            int err     = -errno;
            transferred = (err < 0) ? err : ret;
            break;
        } else {
            if (ret > 0) {
                mem += ret;
                transferred += ret;
                len -= ret;
            }
        }
    }
    return transferred;
}

int32_t network_close(int32_t s) {
    if (s < 0) {
        return -1;
    }
    int res = close(s);
    if (res < 0) {
        int err = -errno;
        return (err < 0) ? err : res;
    }
    return res;
}

int32_t set_blocking(int32_t s, bool blocking) {
    int32_t block = !blocking;
    setsockopt(s, SOL_SOCKET, SO_NONBLOCK, &block, sizeof(block));
    return 0;
}

int32_t network_close_blocking(int32_t s) {
    set_blocking(s, true);
    return network_close(s);
}

int32_t send_exact(int32_t s, char *buf, int32_t length) {
    int buf_size      = length;
    int32_t result    = 0;
    int32_t remaining = length;
    int32_t bytes_transferred;

    uint32_t retryNumber = 0;

    set_blocking(s, true);
    while (remaining) {

    retry:
        bytes_transferred = network_write(s, buf, MIN(remaining, (int) buf_size));

        if (bytes_transferred > 0) {
            remaining -= bytes_transferred;
            buf += bytes_transferred;
        } else if (bytes_transferred < 0) {

            if (retry(bytes_transferred)) {
                OSSleepTicks(OSMillisecondsToTicks(10));
                retryNumber++;
                if (retryNumber <= FTP_RETRIES_NUMBER) goto retry;
            }

            result = bytes_transferred;
            break;
        } else {
            // result = bytes_transferred = 0
            result = bytes_transferred;
            break;
        }
    }
    set_blocking(s, false);

    return result;
}


int32_t send_from_file(int32_t s, client_t *client) {
    // return code
    int32_t result = 0;


    // max value = DEFAULT_NET_BUFFER_SIZE
    int sndBuffSize = DEFAULT_NET_BUFFER_SIZE;
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, &sndBuffSize, sizeof(sndBuffSize));

    // (buffer size = 2*DEFAULT_NET_BUFFER_SIZE)
    int dlBufferSize          = 2 * sndBuffSize;
    char WUT_ALIGNAS(64) *buf = (char *) memalign(0x40, dlBufferSize);
    if (!buf) {
        return -ENOMEM;
    }

    int32_t bytes_read = dlBufferSize;
    while (bytes_read) {

        bytes_read = fread(buf, 1, dlBufferSize, client->f);
        if (bytes_read == 0) {
            // SUCCESS, no more to write
            result = 0;
            break;
        }
        if (bytes_read > 0) {

            uint32_t retryNumber = 0;
            int32_t remaining    = bytes_read;

            // Let buffer on file be larger than socket one for checking performances scenarii
            while (remaining) {

            send_again:
                result = network_write(s, buf, MIN(remaining, dlBufferSize));

                if (result < 0) {
                    if (retry(result)) {
                        retryNumber++;
                        if (retryNumber <= FTP_RETRIES_NUMBER) {
                            OSSleepTicks(OSMillisecondsToTicks(100));
                            goto send_again;
                        } else {
                            retryNumber = 0;
                        }
                    }
                    // result = error, client will be closed
                    break;
                } else {


                    // data block sent sucessfully, continue
                    client->bytesTransferred += result;
                    remaining -= result;
                }
            }
        }
        if (result >= 0) {

            // check bytes read (now because on the last sending, data is already sent here = result)
            if (bytes_read < dlBufferSize) {

                if (bytes_read < 0 || feof(client->f) == 0 || ferror(client->f) != 0) {
                    result = -103;
                    break;
                }
            }

            // result = 0 and EOF
            if ((feof(client->f) != 0) && (result == 0)) {
                // SUCESS : eof file, last data bloc sent
                break;
            }
        }
    }
    free(buf);
    buf = NULL;

    return result;
}

int32_t recv_to_file(int32_t s, client_t *client) {
    // return code
    int32_t result = 0;

    // MAX value = DEFAULT_NET_BUFFER_SIZE
    int rcvBuffSize = DEFAULT_NET_BUFFER_SIZE;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, &rcvBuffSize, sizeof(rcvBuffSize));


    // network_readChunk can overflow but less than (rcvBuffSize*2) bytes
    uint32_t chunckSize = rcvBuffSize;

    // using a buffer size >= 2*rcvBuffSize will handle the overflow
    // (buffer size = 2*DEFAULT_NET_BUFFER_SIZE)
    char WUT_ALIGNAS(64) *buf = (char *) memalign(0x40, 2 * rcvBuffSize);
    if (!buf) {
        return -ENOMEM;
    }

    // Not perfect because it's not aligned, but with the way fclose is called
    // using a custom buffer is annoying to clean up properly
    setvbuf(client->f, NULL, _IOFBF, chunckSize);

    int32_t bytes_read   = chunckSize;
    uint32_t retryNumber = 0;

    while (bytes_read) {
    read_again:
        bytes_read = network_readChunk(s, buf, chunckSize);
        if (bytes_read == 0) {
            result = 0;
            break;
        } else if (bytes_read < 0) {

            if (retry(result)) {
                retryNumber++;
                if (retryNumber <= FTP_RETRIES_NUMBER) {
                    OSSleepTicks(OSMillisecondsToTicks(200));
                    goto read_again;
                } else {
                    retryNumber = 0;
                }
            }
            result = bytes_read;
            break;
        } else {
            // bytes_received > 0

            // write bytes_received to f
            result = fwrite(buf, 1, bytes_read, client->f);
            if ((result < 0 && result < bytes_read) || ferror(client->f) != 0) {
                // error when writing f
                result = -100;
                break;
            } else {
                client->bytesTransferred += result;
            }
        }
    }
    free(buf);
    buf = NULL;

    return result;
}
