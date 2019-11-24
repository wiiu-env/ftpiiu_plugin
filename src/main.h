#ifndef _MAIN_H_
#define _MAIN_H_

/* Main */
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <nsysnet/socket.h>

#define MAXPATHLEN 256

#define WIIU_EAGAIN          EWOULDBLOCK
#define ENODATA         1
#define EISCONN         3
#define EWOULDBLOCK     6
#define EALREADY        10
#define EAGAIN          EWOULDBLOCK
#define EINVAL          11
#define ENOMEM          18
#define EINPROGRESS     22

#define wiiu_geterrno()  (socketlasterr())

//! C wrapper for our C++ functions
int Menu_Main(void);

#ifdef __cplusplus
}
#endif

#endif
