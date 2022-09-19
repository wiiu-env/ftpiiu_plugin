#ifndef _MAIN_H_
#define _MAIN_H_

/* Main */
#ifdef __cplusplus
extern "C" {
#endif

#include "net.h"
#include "version.h"
#include <stdint.h>

#define MAXPATHLEN      256

#define VERSION         "v0.1.1"
#define VERSION_FULL    VERSION VERSION_EXTRA

#define wiiu_geterrno() (errno)

extern bool gSystemFilesAllowed;

//! C wrapper for our C++ functions
int Menu_Main(void);

#ifdef __cplusplus
}
#endif

#endif
