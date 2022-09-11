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
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/fcntl.h>
#include <unistd.h>

#define MIN(x, y) ((x) < (y) ? (x) : (y))

#include "net.h"

#define MAX_NET_BUFFER_SIZE (128 * 1024)
#define MIN_NET_BUFFER_SIZE 4096
# define NET_RETRY_TIME_STEP_MILLISECS 10

extern uint32_t hostIpAddress;

static const uint32_t SOCKET_BUFFER_SIZE = MAX_NET_BUFFER_SIZE;

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
#endif

int32_t network_socket(int32_t domain, int32_t type, int32_t protocol) {
    int sock = socket(domain, type, protocol);
    if (sock < 0) {
        int err = -wiiu_geterrno();
        return (err < 0) ? err : sock;
    }
    if (type == SOCK_STREAM)
    {
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

static int32_t network_readChunk(int32_t s, void *mem, int32_t len) {

    int32_t received = 0;
    int ret = -1;

    // while buffer is not full (len>0)
    while (len>0)
    {
        // max ret value is 2*SOCKET_BUFFER_SIZE
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

int32_t network_write(int32_t s, const void *mem, int32_t len)
{
    int32_t transfered = 0;
    
    while (len)
    {
        int ret = send(s, mem, len, 0);
        if (ret < 0 && wiiu_geterrno() != EAGAIN)
        {
            int err = -wiiu_geterrno();
            transfered = (err < 0) ? err : ret;
            break;
        } else {
            if (ret > 0) {
                mem += ret;
                transfered += ret;
                len -= ret;
            }
        }
    }
    return transfered;
}

int32_t network_close(int32_t s) {
    if (s < 0) {
        return -1;
    }

    int res = close(s);

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


int32_t send_exact(int32_t s, char *buf, int32_t length) {
    int buf_size = length;
    int32_t result = 0;
    int32_t remaining = length;
    int32_t bytes_transferred;
    
    set_blocking(s, true);
    while (remaining) {

        bytes_transferred = network_write(s, buf, MIN(remaining, (int) buf_size));
        
        if (bytes_transferred > 0) {
            remaining -= bytes_transferred;
            buf += bytes_transferred;
        } else if (bytes_transferred < 0) {
            
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


int32_t send_from_file(int32_t s, FILE *f) {
    // return code
    int32_t result = 0;
    
    // max value for SNDBUF = SOCKET_BUFFER_SIZE (the system double the value set)
    int sndBuffSize = SOCKET_BUFFER_SIZE/2;
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, &sndBuffSize, sizeof(sndBuffSize));

    int32_t dlBufferSize = sndBuffSize*2;
	
    char *buf = (char *) memalign(0x40, dlBufferSize);
    if (!buf)
        return -1;


    int32_t bytes_read = dlBufferSize;        
	while (bytes_read) {

        bytes_read = fread(buf, 1, dlBufferSize, f);
        if (bytes_read == 0) {
            // SUCCESS, no more to write                  
            result = 0;
            break;
        }        
        if (bytes_read > 0) {
        
            int32_t remaining = bytes_read;            

            // Let buffer on file be larger than socket one for checking performances scenarii 
            while (remaining) {
            
                result = network_write(s, buf, MIN(remaining, dlBufferSize));

                
                if (result < 0) {
                    // result = error, connection will be closed
                    break;
                } else {
                
                    remaining -= result;                    
                                        
                }
            }        
		}
        if (result >=0) {
                
            // check bytes read (now because on the last sending, data is already sent here = result)
            if (bytes_read < dlBufferSize) {
                    
            	if (bytes_read < 0 || feof(f) == 0 || ferror(f) != 0) {
                    // ERROR : not on eof file or read error, or error on stream => ERROR
                    result = -103;
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
    return result;
}

int32_t recv_to_file(int32_t s, FILE *f) {
    // return code
    int32_t result = 0;
    
    // (the system double the value set)
    int rcvBuffSize = SOCKET_BUFFER_SIZE;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, &rcvBuffSize, sizeof(rcvBuffSize));
    
    char *buf = (char *) memalign(0x40, rcvBuffSize);
    if (!buf) {
        return -1;
	}

	// network_readChunk can overflow but less than (rcvBuffSize*2) bytes
	// considering a buffer size of UL_BUFFER_SIZE - (rcvBuffSize*2) with UL_BUFFER_SIZE >= 2*(rcvBuffSize*2) will handle the overflow
	uint32_t chunckSize = 8*SOCKET_BUFFER_SIZE - (rcvBuffSize*2);
    int32_t bytes_received = chunckSize;
    while (bytes_received) {
                        
        bytes_received = network_readChunk(s, buf, chunckSize);
        
        if (bytes_received == 0) {
            // SUCCESS, no more to write to file
            result = 0;
                        
        } else if (bytes_received < 0) {

            // result = error, connection will be closed
            result = bytes_received;
            break;
        } else {
            // bytes_received > 0
 
            // write bytes_received to f
            result = fwrite(buf, 1, bytes_received, f);
            
            if ((result < 0 && result < bytes_received) || ferror(f) != 0) {
                // error when writing f
                result = -100;
                break;
            }
		}
    }
    free(buf);
    return result;
}
