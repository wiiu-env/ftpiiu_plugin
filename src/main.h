#ifndef _MAIN_H_
#define _MAIN_H_

/* Main */
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "net.h"

#define MAXPATHLEN 256

#define wiiu_geterrno()  (errno)

//! C wrapper for our C++ functions
int Menu_Main(void);

#ifdef __cplusplus
}
#endif

#endif
