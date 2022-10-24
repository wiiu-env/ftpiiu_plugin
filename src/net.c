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

#define DEFAULT_NET_BUFFER_SIZE (128 * 1024)
#define MIN_NET_BUFFER_SIZE     (4 * 1024)

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

int32_t network_socket(int32_t domain, int32_t type, int32_t protocol) {
    int sock = socket(domain, type, protocol);
    if (sock < 0) {
        int err = -wiiu_geterrno();
        return (err < 0) ? err : sock;
    }
    if (type == SOCK_STREAM) {
        int enable = 1;
        // Activate WinScale
        setsockopt(sock, SOL_SOCKET, SO_WINSCALE, &enable, sizeof(enable));
    }
    return sock;
}

int32_t network_bind(int32_t s, struct sockaddr *name, int32_t namelen) {
    int res = bind(s, name, namelen);
    if (res < 0) {
        int err = -wiiu_geterrno();
        return (err < 0) ? err : res;
    }
    return res;
}

int32_t network_listen(int32_t s, uint32_t backlog) {
    int res = listen(s, backlog);
    if (res < 0) {
        int err = -wiiu_geterrno();
        return (err < 0) ? err : res;
    }
    return res;
}

int32_t network_accept(int32_t s, struct sockaddr *addr, socklen_t *addrlen) {
    int res = accept(s, addr, addrlen);
    if (res < 0) {
        int err = -wiiu_geterrno();
        return (err < 0) ? err : res;
    }
    return res;
}

int32_t network_connect(int32_t s, struct sockaddr *addr, int32_t addrlen) {
    int res = connect(s, addr, addrlen);
    if (res < 0) {
        int err = -wiiu_geterrno();
        return (err < 0) ? err : res;
    }
    return res;
}

int32_t network_read(int32_t s, void *mem, int32_t len) {
    int res = recv(s, mem, len, 0);
    if (res < 0) {
        int err = -wiiu_geterrno();
        return (err < 0) ? err : res;
    }
    return res;
}

// read from network by chunk (len long)
static int32_t network_readChunk(int32_t s, void *mem, int32_t len) {

    int32_t received = 0;
    int ret = -1;

    // while buffer is not full (len>0)
    while (len>0)
    {
        // max ret value is 2*setsockopt value on SO_RCVBUF
        ret = recv(s, mem, len, 0);
        if (ret == 0) {
            // client EOF detected
            break;
        } else if (ret < 0 && wiiu_geterrno() != EAGAIN) {
            int err = -wiiu_geterrno();
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
    int32_t transfered = 0;

    while (len) {
        int ret = send(s, mem, len, 0);
        if (ret < 0) {
            int err    = -wiiu_geterrno();
            transfered = (err < 0) ? err : ret;
            break;
        }

        mem += ret;
        transfered += ret;
        len -= ret;
    }
    return transfered;
}

int32_t network_close(int32_t s) {
    if (s < 0) {
        return -1;
    }
    int res = close(s);
    shutdown(s, SHUT_RDWR);
    if (res < 0) {
        int err = -wiiu_geterrno();
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

int32_t create_server(uint16_t port) {
    int32_t server = network_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (server < 0) {
        return -1;
    }

    set_blocking(server, false);
    uint32_t enable = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

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
    if ((ret = network_listen(server, 3)) < 0) {
        network_close(server);
        //gxprintf("Error listening on socket: [%i] %s\n", -ret, strerror(-ret));
        return ret;
    }

    return server;
}

typedef int32_t (*transferrer_type)(int32_t s, void *mem, int32_t len);

static int32_t transfer_exact(int32_t s, char *buf, int32_t length, transferrer_type transferrer) {
    int32_t result    = 0;
    int32_t remaining = length;
    int32_t bytes_transferred;
    set_blocking(s, true);
    uint32_t curNetBufferSize = DEFAULT_NET_BUFFER_SIZE;

    while (remaining) {
    try_again_with_smaller_buffer:
        bytes_transferred = transferrer(s, buf, MIN(remaining, (int) DEFAULT_NET_BUFFER_SIZE));
        if (bytes_transferred > 0) {
            remaining -= bytes_transferred;
            buf += bytes_transferred;
        } else if (bytes_transferred < 0) {
            if (bytes_transferred == -EINVAL && curNetBufferSize == DEFAULT_NET_BUFFER_SIZE) {
                curNetBufferSize = MIN_NET_BUFFER_SIZE;
                OSSleepTicks(OSMillisecondsToTicks(1));
                goto try_again_with_smaller_buffer;
            }
            if (bytes_transferred == -EAGAIN) {
                OSSleepTicks(OSMillisecondsToTicks(1));
                continue;
            }
            result = bytes_transferred;
            break;
        } else {
            result = -ENODATA;
            break;
        }
    }
    set_blocking(s, false);
    return result;
}

int32_t send_exact(int32_t s, char *buf, int32_t length) {
    return transfer_exact(s, buf, length, (transferrer_type) network_write);
}

int32_t send_from_file(int32_t s, FILE *f) {
    int32_t result = 0;

    // (the system double the value set)
    int bufSize = DEFAULT_NET_BUFFER_SIZE;
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, &bufSize, sizeof(bufSize));

	int dlBuffer = 2*bufSize;
    char *buf = (char *) memalign(0x40, dlBuffer);
    if (!buf) {
        return -ENOMEM;
    }
       
    int32_t bytes_read = dlBuffer;        
	while (bytes_read) {

	    bytes_read = fread(buf, 1, dlBuffer, f);
        if (bytes_read == 0) {
            // SUCCESS, no more to write                  
            result = 0;
            break;
        }        
	    if (bytes_read > 0) {
	        result = send_exact(s, buf, bytes_read);
	        if (result < 0)
	            break;
	    }
        if (result >=0) {
            // check bytes read (now because on the last sending, data is already sent here = result)
            if (bytes_read < dlBuffer) {
                    
            	if (bytes_read < 0 || feof(f) == 0 || ferror(f) != 0) {
                    result = -3;
                    break;
                }
            }
            
            // result = 0 and EOF
            if ((feof(f) != 0) && (result == 0)) {
                // SUCESS : eof file, last data bloc sent
                break;
            }
        }
    }
    free(buf);
    buf = NULL;
    return result;
}

int32_t recv_to_file(int32_t s, FILE *f) {
    // return code
    int32_t result = 0;

    // (the system double the value set)
    int rcvBuffSize = DEFAULT_NET_BUFFER_SIZE;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, &rcvBuffSize, sizeof(rcvBuffSize));

	// network_readChunk can overflow but less than (rcvBuffSize*2) bytes = size of SO_RCVBUF
	// use a buffer size >= rcvBuffSize*2 to handle the overflow
	int ulBuffer = 2*rcvBuffSize;	
    char *buf = (char *) memalign(0x40, ulBuffer);
    if (!buf) {
        return -ENOMEM;
    }

	// size of the network read chunk = ulBuffer - (rcvBuffSize*2)
 	uint32_t chunckSize = DEFAULT_NET_BUFFER_SIZE;


    // Not perfect because it's not aligned, but with the way fclose is called
    // using a custom buffer is annoying to clean up properly
    setvbuf(f, NULL, _IOFBF, chunckSize);

    int32_t bytes_read = chunckSize;
    while (bytes_read) {
    try_again:
        bytes_read = network_readChunk(s, buf, chunckSize);
        if (bytes_read == 0) {
			result = 0;
			break;
		} else if (bytes_read < 0) {

            if (bytes_read == -EINVAL) {
                 OSSleepTicks(OSMillisecondsToTicks(1));
                goto try_again;
            }
			result = bytes_read;
            break;
        } else {
            // bytes_received > 0

            // write bytes_received to f
            result = fwrite(buf, 1, bytes_read, f);
            if ((result < 0 && result < bytes_read) || ferror(f) != 0) {
                // error when writing f
                result = -100;
                break;
            }
 		}
	}
	free(buf);
	buf = NULL;

	return result;
}
