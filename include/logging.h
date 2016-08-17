/* A simple logging macro and some wrappers */

#pragma once

/*
 * The following requirements are for the "DEBUG" mode, which flushes
 * everything to disk after each msg and syncs. If you do not wish this
 * behaviour, which will incur in significant performance loss for "DEBUG"
 * mode, remove these guards and edit the LOGGING macro, removing the fflush
 * and fsync calls.
 */
#ifndef __unix__
  #warning POSIX is assumed. System does not appear to be UNIX.
#endif

#if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200112L
  #warning _POSIX_C_SOURCE should be >= 200112L
#endif

#include <features.h>

#ifndef __GLIBC__
  #warning glibc not present. Required features for logging might be missing.
#endif

#include <unistd.h>
#include <stdio.h>

enum logging_mode {
  LOG_LEVEL_DEBUG,
  LOG_LEVEL_INFO,
  LOG_LEVEL_WARNING,
  LOG_LEVEL_ERROR,
  LOG_LEVEL_CRITICAL
};

/* Messages of this level or lower will be logged */
// #define LOG_LEVEL LOG_LEVEL_WARNING
/* Optional, if you want to use a constant log file */
#define LOG_FILE stderr

/* Use this prefix in the log messages */
#define LOG_PREFIX __func__

/* No terminal colors */
/*
static char const LOG_LEVEL_DEBUG_STR[] = "DEBUG!";
static char const LOG_LEVEL_INFO_STR[] = "INFO!";
static char const LOG_LEVEL_WARNING_STR[] = "WARNING!";
static char const LOG_LEVEL_ERROR_STR[] = "ERROR!";
static char const LOG_LEVEL_CRITICAL_STR[] = "CRITICAL!";
*/
/* Terminal colors */
static char const LOG_LEVEL_DEBUG_STR[] = "DEBUG!";
static char const LOG_LEVEL_INFO_STR[] = "\033[4;37mINFO!\033[00m";
static char const LOG_LEVEL_WARNING_STR[] = "\033[0;33mWARNING!\033[00m";
static char const LOG_LEVEL_ERROR_STR[] = "\033[0;31mERROR!\033[00m";
static char const LOG_LEVEL_CRITICAL_STR[] = "\033[4;31mCRITICAL!\033[00m";

static char const * const LOG_ARR[] = {
  LOG_LEVEL_DEBUG_STR,
  LOG_LEVEL_INFO_STR,
  LOG_LEVEL_WARNING_STR,
  LOG_LEVEL_ERROR_STR,
  LOG_LEVEL_CRITICAL_STR,
};

#define LOGGING(log_file, log_mode, ...)\
  do {\
    if (LOG_LEVEL <= (log_mode)) { \
      fprintf((log_file), "%s: %s ", LOG_PREFIX, LOG_ARR[(log_mode)]);\
      fprintf((log_file), ##__VA_ARGS__);\
    }\
    if (LOG_LEVEL <= LOG_LEVEL_DEBUG) {\
      fflush((log_file));\
      fsync(fileno((log_file)));\
    }\
  } while(0)

/* The following wrapper macros write to LOG_FILE if LOG_LEVEL <= something */

#ifdef LOG_FILE

  #define LOG_CRITICAL(...)\
  LOGGING(LOG_FILE, LOG_LEVEL_CRITICAL, ##__VA_ARGS__);

  #define LOG_ERROR(...)\
  LOGGING(LOG_FILE, LOG_LEVEL_ERROR, ##__VA_ARGS__);

  #define LOG_WARNING(...)\
  LOGGING(LOG_FILE, LOG_LEVEL_WARNING, ##__VA_ARGS__);

  #define LOG_INFO(...)\
  LOGGING(LOG_FILE, LOG_LEVEL_INFO, ##__VA_ARGS__);

  #define LOG_DEBUG(...)\
  LOGGING(LOG_FILE, LOG_LEVEL_DEBUG, ##__VA_ARGS__);

#endif /* LOG_FILE */

/* Other stuff */

#include <stdlib.h>
#include <errno.h>

#define REPORT_AND_EXIT()\
  do {\
    perror(__func__);\
    exit(EXIT_FAILURE);\
  } while(0)

#define LOG_AND_EXIT(...)\
  do {\
    LOG_CRITICAL(__VA_ARGS__);\
    exit(EXIT_FAILURE);\
  }while(0)
