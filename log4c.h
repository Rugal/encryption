#ifndef SCLOG4C_H
#define SCLOG4C_H

#include <limits.h>
#include <stdio.h>

enum Log4CLevel {
  LOG4C_ALL     = INT_MIN,
  LOG4C_TRACE   = 0,
  LOG4C_DEBUG   = 10,
  LOG4C_INFO    = 20,
  LOG4C_WARNING = 30,
  LOG4C_ERROR   = 40,
  LOG4C_OFF     = INT_MAX
};

extern int log4c_level;

#define LOG(level, fmt, ...) \
  if (level >= log4c_level) { \
    fprintf(stderr, "%s:%d: %d: In function %s: " fmt "\n", __FILE__, __LINE__, level, __func__, ##__VA_ARGS__); \
  }

#endif

