#pragma once

#include <coreinit/debug.h>
#include <string.h>
#include <whb/log.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_APP_TYPE "P"
#define LOG_APP_NAME "ftpiiu_plugin"

#define __FILENAME_X__ (strrchr (__FILE__, '\\') ? strrchr (__FILE__, '\\') + 1 : __FILE__)
#define __FILENAME__ (strrchr (__FILE__, '/') ? strrchr (__FILE__, '/') + 1 : __FILENAME_X__)

#define LOG(LOG_FUNC, FMT, ARGS...) LOG_EX_DEFAULT (LOG_FUNC, "", "", "", FMT, ##ARGS)

#define CONSOLE_COLOR_RED "\033[31m"
#define CONSOLE_COLOR_YELLOW "\033[33m"
#define CONSOLE_COLOR_CYAN "\033[36m"
#define CONSOLE_COLOR_RESET "\033[0m"

#define LOG_EX_DEFAULT(LOG_FUNC, LOG_COLOR, LOG_LEVEL, LINE_END, FMT, ARGS...)                     \
	LOG_EX (__FILENAME__,                                                                          \
	    __FUNCTION__,                                                                              \
	    __LINE__,                                                                                  \
	    LOG_FUNC,                                                                                  \
	    LOG_COLOR,                                                                                 \
	    LOG_LEVEL,                                                                                 \
	    LINE_END,                                                                                  \
	    FMT,                                                                                       \
	    ##ARGS)

#define LOG_EX(FILENAME, FUNCTION, LINE, LOG_FUNC, LOG_COLOR, LOG_LEVEL, LINE_END, FMT, ARGS...)   \
	do                                                                                             \
	{                                                                                              \
		LOG_FUNC (LOG_COLOR "[(%s)%18s][%23s]%30s@L%04d: " LOG_LEVEL "" FMT "" LINE_END,           \
		    LOG_APP_TYPE,                                                                          \
		    LOG_APP_NAME,                                                                          \
		    FILENAME,                                                                              \
		    FUNCTION,                                                                              \
		    LINE,                                                                                  \
		    ##ARGS);                                                                               \
	} while (0)

#ifdef DEBUG

#ifdef VERBOSE_DEBUG
#define DEBUG_FUNCTION_LINE_VERBOSE(FMT, ARGS...) LOG (WHBLogPrintf, FMT, ##ARGS)
#define DEBUG_FUNCTION_LINE_VERBOSE_EX(FILENAME, FUNCTION, LINE, FMT, ARGS...)                     \
	LOG_EX (FILENAME, FUNCTION, LINE, WHBLogPrintf, "", "", FMT, ##ARGS);
#else
#define DEBUG_FUNCTION_LINE_VERBOSE(FMT, ARGS...) while (0)
#define DEBUG_FUNCTION_LINE_VERBOSE_EX(FMT, ARGS...) while (0)
#endif

#define DEBUG_FUNCTION_LINE(FMT, ARGS...) LOG (WHBLogPrintf, FMT, ##ARGS)

#define DEBUG_FUNCTION_LINE_WRITE(FMT, ARGS...) LOG (WHBLogWritef, FMT, ##ARGS)

#define DEBUG_FUNCTION_LINE_ERR(FMT, ARGS...)                                                      \
	LOG_EX_DEFAULT (                                                                               \
	    WHBLogPrintf, CONSOLE_COLOR_RED, "## ERROR## ", CONSOLE_COLOR_RESET, FMT, ##ARGS)
#define DEBUG_FUNCTION_LINE_WARN(FMT, ARGS...)                                                     \
	LOG_EX_DEFAULT (                                                                               \
	    WHBLogPrintf, CONSOLE_COLOR_YELLOW, "##WARN ## ", CONSOLE_COLOR_RESET, FMT, ##ARGS)
#define DEBUG_FUNCTION_LINE_INFO(FMT, ARGS...)                                                     \
	LOG_EX_DEFAULT (                                                                               \
	    WHBLogPrintf, CONSOLE_COLOR_CYAN, "##INFO ## ", CONSOLE_COLOR_RESET, FMT, ##ARGS)

#define DEBUG_FUNCTION_LINE_ERR_LAMBDA(FILENAME, FUNCTION, LINE, FMT, ARGS...)                     \
	LOG_EX (FILENAME,                                                                              \
	    FUNCTION,                                                                                  \
	    LINE,                                                                                      \
	    WHBLogPrintf,                                                                              \
	    CONSOLE_COLOR_RED,                                                                         \
	    "##ERROR## ",                                                                              \
	    CONSOLE_COLOR_RESET,                                                                       \
	    FMT,                                                                                       \
	    ##ARGS);

#else

#define DEBUG_FUNCTION_LINE_VERBOSE_EX(FMT, ARGS...) while (0)

#define DEBUG_FUNCTION_LINE_VERBOSE(FMT, ARGS...) while (0)

#define DEBUG_FUNCTION_LINE(FMT, ARGS...) while (0)

#define DEBUG_FUNCTION_LINE_WRITE(FMT, ARGS...) while (0)

#define DEBUG_FUNCTION_LINE_ERR(FMT, ARGS...)                                                      \
	LOG_EX_DEFAULT (                                                                               \
	    OSReport, CONSOLE_COLOR_RED, "##ERROR## ", CONSOLE_COLOR_RESET "\n", FMT, ##ARGS)
#define DEBUG_FUNCTION_LINE_WARN(FMT, ARGS...)                                                     \
	LOG_EX_DEFAULT (                                                                               \
	    OSReport, CONSOLE_COLOR_YELLOW, "##WARN ## ", CONSOLE_COLOR_RESET "\n", FMT, ##ARGS)
#define DEBUG_FUNCTION_LINE_INFO(FMT, ARGS...)                                                     \
	LOG_EX_DEFAULT (                                                                               \
	    OSReport, CONSOLE_COLOR_CYAN, "##INFO ## ", CONSOLE_COLOR_RESET "\n", FMT, ##ARGS)

#define DEBUG_FUNCTION_LINE_ERR_LAMBDA(FILENAME, FUNCTION, LINE, FMT, ARGS...)                     \
	LOG_EX (FILENAME,                                                                              \
	    FUNCTION,                                                                                  \
	    LINE,                                                                                      \
	    OSReport,                                                                                  \
	    CONSOLE_COLOR_RED,                                                                         \
	    "##ERROR## ",                                                                              \
	    CONSOLE_COLOR_RESET "\n",                                                                  \
	    FMT,                                                                                       \
	    ##ARGS);

#endif

void initLogging ();

void deinitLogging ();

#ifdef __cplusplus
}
#endif
