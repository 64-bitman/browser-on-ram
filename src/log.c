#define _GNU_SOURCE
#include "log.h"
#include "config.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

enum LogLevel LOG_LEVEL = LOG_INFO;

static const char *log_str[] = { "DEBUG", "INFO", "WARN", "ERROR" };

static FILE *LOG_FILE = NULL;

// allow for at max MAX_LOG_ENTRIES entries in log file
static int truncate_log_file(void)
{
        char tmp_path[PATH_MAX];
        char log_path[PATH_MAX];

        snprintf(tmp_path, PATH_MAX, "%s/.tmp.txt", PATHS.logs);
        snprintf(log_path, PATH_MAX, "%s/log.txt", PATHS.logs);

        // count number of entries
        char *line = NULL;
        size_t size = 0;
        ssize_t nread = 0;
        long int count = 0;

        rewind(LOG_FILE);

        while ((nread = getline(&line, &size, LOG_FILE)) != -1) {
                if (line[0] == '<') {
                        count++;
                }
        }

        // tmp file where log lines will be written to
        FILE *tmp_fp = fopen(tmp_path, "w");

        if (tmp_fp == NULL) {
                return -1;
        }

        long int entries_to_be_rm =
                count - (long int)CONFIG.max_log_entries + 1;

        // if negative then we have less than max entries
        if (entries_to_be_rm < 0) {
                goto exit;
        }

        rewind(LOG_FILE);
        count = 0;
        // skip entries that should be removed
        while ((nread = getline(&line, &size, LOG_FILE)) != -1) {
                if (line[0] == '<') {
                        count++;
                }
                if (count > entries_to_be_rm) {
                        fprintf(tmp_fp, "%s", line);
                }
        }

        // swap tmp and log file
        if (renameat2(AT_FDCWD, log_path, AT_FDCWD, tmp_path,
                      RENAME_EXCHANGE) == -1) {
                goto exit;
        }

        // point log file fp to new file
        fclose(LOG_FILE);
        LOG_FILE = fopen(log_path, "a+");

        if (LOG_FILE == NULL) {
                goto exit;
        }

exit:
        fclose(tmp_fp);
        unlink(tmp_path);
        free(line);
        fseek(LOG_FILE, 0L, SEEK_END);

        return 0;
}

// should be run after paths and config have been initialized
// and log directory created
int init_logger(void)
{
        char log_path[PATH_MAX];

        snprintf(log_path, PATH_MAX, "%s/log.txt", PATHS.logs);

        if (CONFIG.max_log_entries == 0) {
                unlink(log_path);
                return 0;
        }

        LOG_FILE = fopen(log_path, "a+");

        if (LOG_FILE == NULL) {
                return -1;
        }

        // print current time into file
        time_t unixtime = time(NULL);
        struct tm *time_info = NULL;

        if (unixtime == (time_t)-1 ||
            (time_info = localtime(&unixtime)) == NULL) {
                return -1;
        }
        char time_buf[100];

        strftime(time_buf, 100, "%d-%m-%y %H:%M:%S", time_info);

        if (CONFIG.max_log_entries > 0) {
                if (truncate_log_file() == -1) {
                        return -1;
                }
        }
        // print header with current time
        fprintf(LOG_FILE, "\n<%s>\n", time_buf);

        return 0;
}

void plog(enum LogLevel level, const char *format, ...)
{
        va_list args;

        if (LOG_FILE != NULL) {
                va_start(args, format);

                fprintf(LOG_FILE, "%5s: ", log_str[level]);
                vfprintf(LOG_FILE, format, args);
                fprintf(LOG_FILE, "\n");

                fflush(LOG_FILE);

                va_end(args);
        }
        if (LOG_LEVEL > level) {
                return;
        }

        va_start(args, format);

        fprintf(stderr, "%5s: ", log_str[level]);
        vfprintf(stderr, format, args);
        fprintf(stderr, "\n");

        va_end(args);
}

// vim: sw=8 ts=8
