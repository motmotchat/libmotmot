#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
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
log_level(enum log_level level, const char *fmt, ...)
{
  va_list va;
  time_t now;
  struct tm *local;
  char buf[25];

  assert(level < 4);

  now = time(NULL);
  local = localtime(&now);

  strftime(buf, sizeof(buf), "%Y:%m:%d %H:%H:%S", local);

  va_start(va, fmt);
  if (isatty(2)) {
    fprintf(stderr, "%s[%s] ", colors[level], buf);
    vfprintf(stderr, fmt, va);
    fprintf(stderr, "\x1b[0m\n");
  } else {
    fprintf(stderr, "[%s] ", buf);
    vfprintf(stderr, fmt, va);
    fprintf(stderr, "\n");
  }
  va_end(va);
}

void
log_errno(const char *msg)
{
  log_error("%s: %s", msg, strerror(errno));
}
