#ifndef __LOG_H__
#define __LOG_H__

#define PRINTFLIKE(fmt, arg) __attribute__((format(printf, fmt, arg)))

enum log_level {
  LOG_INFO,
  LOG_WARN,
  LOG_ERROR,
  LOG_FATAL
};

void log_level(enum log_level level, const char *fmt, ...) PRINTFLIKE(2, 3);

#define log_info(...) log_level(LOG_INFO, __VA_ARGS__)
#define log_warn(...) log_level(LOG_WARN, __VA_ARGS__)
#define log_error(...) log_level(LOG_ERROR, __VA_ARGS__)
#define log_fatal(...) log_level(LOG_FATAL, __VA_ARGS__)

void log_errno(const char *msg);

#endif // __LOG_H__
