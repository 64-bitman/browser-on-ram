#ifndef SRC_INCLUDE_UTIL_H
#define SRC_INCLUDE_UTIL_H

#include <sys/types.h>

enum LogLevel { LOG_ERROR, LOG_WARN, LOG_INFO, LOG_DEBUG };

extern enum LogLevel LOG_LEVEL;

void log_print (enum LogLevel level, const char *file, const int line,
                const char *format, ...);
char *print2string (const char *format, ...);
char *trim (char *str);
int copy_r (const char *src, const char *dest);
int remove_r (const char *path);
char *filename_wtime (const char *filename, const char *str);
pid_t pgrep (const char *name);
off_t get_dir_size (const char *path);
char *human_readable (off_t bytes);
char *replace_char (char *str, char target, char replace);
int systemd_userservice_active (const char *name);
int mkdir_p (const char *path, mode_t mode);
int move (const char *oldpath, const char *newpath);
int get_bool (const char *value);

#define __TO_STRING(s) #s
#define TO_STRING(s) __TO_STRING (s)

#define EXISTS(path) (stat (path, &sb) == 0)
#define DIREXISTS(path) (lstat (path, &sb) == 0 && S_ISDIR (sb.st_mode))
#define SYMEXISTS(path) (lstat (path, &sb) == 0 && S_ISLNK (sb.st_mode))
#define LEXISTS(path) (lstat (path, &sb) == 0)

#define LOG(level, format, ...)                                               \
    log_print (level, __FILE__, __LINE__, format, ##__VA_ARGS__)

// ansi escape codes
#define BOLD "\e[1m"
#define DIM "\e[2m"
#define ITALIC "\e[3m"
#define UNDERLINE "\e[4m"
#define RESET "\e[0m"

#define RED "\e[31m"
#define GREEN "\e[32m"
#define YELLOW "\e[33m"
#define BLUE "\e[34m"
#define BLUE "\e[34m"

#ifdef DEBUG

    #define PERROR() perror (__FILE__ ":" TO_STRING (__LINE__) " Error");
    #define LOGCWD()                                                          \
        do {                                                                  \
            int perrno = errno;                                               \
            char *CWD = get_current_dir_name ();                              \
            LOG (LOG_DEBUG, "Current working directory is %s", CWD);          \
            errno = perrno;                                                   \
        } while (0)

#else

    #define PERROR()                                                          \
        do {                                                                  \
        } while (0)
    #define LOGCWD()                                                          \
        do {                                                                  \
        } while (0)

#endif
#endif
