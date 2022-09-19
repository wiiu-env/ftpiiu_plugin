#ifndef _MAIN_H_
#define _MAIN_H_

/* Main */
#ifdef __cplusplus
extern "C" {
#endif

#include "net.h"
#include "version.h"
#include <stdint.h>

#define MAXPATHLEN       256

#define VERSION_RAW      "0.1.1"
#define VERSION_FULL_RAW VERSION_RAW VERSION_EXTRA

#define wiiu_geterrno()  (errno)

//! C wrapper for our C++ functions
int Menu_Main(void);

#ifdef __cplusplus
}
#endif

#endif
