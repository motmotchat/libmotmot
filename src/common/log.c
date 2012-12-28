#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "log.h"

const char *colors[4] = {
  "",               // LOG_INFO: No color
  "\x1b[33m",       // LOG_WARN: Yellow
  "\x1b[31m",       // LOG_ERROR: Red
  "\x1b[5;30;41m",  // LOG_FATAL: Black on red, blinking
};

void
vlog_level(enum log_level level, const char *fmt, va_list va)
{
  time_t now;
  struct tm *local;
  char buf[25];

  assert(level < 4);

  now = time(NULL);
  local = localtime(&now);

  strftime(buf, sizeof(buf), "%Y:%m:%d %H:%H:%S", local);

  if (isatty(2)) {
    fprintf(stderr, "%s[%s] ", colors[level], buf);
    vfprintf(stderr, fmt, va);
    fprintf(stderr, "\x1b[0m\n");
  } else {
    fprintf(stderr, "[%s] ", buf);
    vfprintf(stderr, fmt, va);
    fprintf(stderr, "\n");
  }
}

void
log_level(enum log_level level, const char *fmt, ...)
{
  va_list va;

  va_start(va, fmt);
  vlog_level(level, fmt, va);
  va_end(va);
}

void
log_errno(const char *msg)
{
  log_error("%s: %s", msg, strerror(errno));
}

void
log_assert(bool cond, const char *fmt, ...)
{
  va_list va;

  if (cond) { return; }

  va_start(va, fmt);
  vlog_level(LOG_ERROR, fmt, va);
  va_end(va);

  exit(1);
}
