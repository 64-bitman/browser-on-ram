#ifndef SRC_INCLUDE_UTIL_H
#define SRC_INCLUDE_UTIL_H

#include <sys/types.h>

void log_print (int level, const char *file, const int line,
                const char *format, ...);
char *print2string (const char *format, ...);
char *trim (char *str);
int copy_r (const char *src, const char *dest);
int remove_r (const char *path);
char *create_unique_filename (const char *filename, const char *str);
pid_t pgrep (const char *name);
off_t get_dir_size (const char *path);
char *human_readable (off_t bytes);
char *replace_char (char *str, char target, char replace);
int systemd_userservice_active (const char *name);

extern int LOG_LEVEL;

#define __TO_STRING(s) #s
#define TO_STRING(s) __TO_STRING (s)

#define LOG_DEBUG 3
#define LOG_INFO 2
#define LOG_WARN 1
#define LOG_ERROR 0

#define EXISTS(path) stat (path, &sb) == 0

#define LOG(level, format, ...)                                               \
    log_print (level, __FILE__, __LINE__, format, ##__VA_ARGS__)

#define CHECKALLOC(var, do_return)                                            \
    do {                                                                      \
        errno = 0;                                                            \
        if (var == NULL) {                                                    \
            LOG (LOG_ERROR, "memory allocation failed");                      \
            PERROR ();                                                        \
            if (do_return) {                                                  \
                return -1;                                                    \
            }                                                                 \
        }                                                                     \
    } while (0)

#ifdef DEBUG

#define PERROR() perror (__FILE__ ":" TO_STRING (__LINE__) " Error");
#define LOGCWD()                                                              \
    do {                                                                      \
        int perrno = errno;                                                   \
        char *CWD = get_current_dir_name ();                                  \
        LOG (LOG_DEBUG, "Current working directory is %s", CWD);              \
        errno = perrno;                                                       \
    } while (0)

#else

#define PERROR()                                                              \
    do {                                                                      \
    } while (0)
#define LOGCWD()                                                              \
    do {                                                                      \
    } while (0)

#endif
#endif
