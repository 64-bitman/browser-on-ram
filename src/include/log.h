#pragma once

enum LogLevel { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };

extern enum LogLevel LOG_LEVEL;

int init_logger(void);
void plog(enum LogLevel level, const char *format, ...);

// vim: sw=8 ts=8
