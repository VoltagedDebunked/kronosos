#ifndef LOG_H
#define LOG_H

#include <stdarg.h>
#include <stdbool.h>

typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_CRITICAL
} log_level_t;

bool log_init(log_level_t level);
void log_printf(log_level_t level, const char *fmt, ...);
void log_message(log_level_t level, const char *msg);

// Convenience macros
#define LOG_DEBUG(fmt, ...) log_printf(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) log_printf(LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) log_printf(LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) log_printf(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LOG_CRITICAL(fmt, ...) log_printf(LOG_LEVEL_CRITICAL, fmt, ##__VA_ARGS__)

#define LOG_DEBUG_MSG(msg) log_message(LOG_LEVEL_DEBUG, msg)
#define LOG_INFO_MSG(msg) log_message(LOG_LEVEL_INFO, msg)
#define LOG_WARN_MSG(msg) log_message(LOG_LEVEL_WARN, msg)
#define LOG_ERROR_MSG(msg) log_message(LOG_LEVEL_ERROR, msg)
#define LOG_CRITICAL_MSG(msg) log_message(LOG_LEVEL_CRITICAL, msg)

#endif // LOG_H