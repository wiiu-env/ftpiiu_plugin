#ifndef _MAIN_H_
#define _MAIN_H_

/* Main */
#ifdef __cplusplus
extern "C" {
#endif

#include "net.h"
#include "version.h"
#include <stdint.h>

#define VERSION         "v0.4b"
#define VERSION_FULL    VERSION VERSION_EXTRA

extern bool gSystemFilesAllowed;

//! C wrapper for our C++ functions
int Menu_Main(void);

#ifdef __cplusplus
}
#endif

#endif
