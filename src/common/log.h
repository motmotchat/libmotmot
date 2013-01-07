#ifndef __TRILL_LOG_H__
#define __TRILL_LOG_H__

#include <stdbool.h>

#define PRINTFLIKE(fmt, arg) __attribute__((format(printf, fmt, arg)))

enum log_level {
  LOG_FATAL,
  LOG_ERROR,
  LOG_WARN,
  LOG_INFO,
  LOG_DEBUG,
};

void log_level(enum log_level level, const char *fmt, ...) PRINTFLIKE(2, 3);

#define log_fatal(...) log_level(LOG_FATAL, __VA_ARGS__)
#define log_error(...) log_level(LOG_ERROR, __VA_ARGS__)
#define log_warn(...) log_level(LOG_WARN, __VA_ARGS__)
#define log_info(...) log_level(LOG_INFO, __VA_ARGS__)
#define log_debug(...) log_level(LOG_DEBUG, __VA_ARGS__)

void log_errno(const char *msg);
void log_assert(bool cond, const char *fmt, ...) PRINTFLIKE(2, 3);

#endif // __TRILL_LOG_H__
