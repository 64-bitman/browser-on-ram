#ifndef SRC_INCLUDE_UTIL_H
#define SRC_INCLUDE_UTIL_H

/* #define TO_STRING(s) #s */
/* #define HOME_PATH TO_STRING(HOME) */
/* #define TMPDIR_PATH TO_STRING(BOR_TMPDIR) */
/* #define SH_DIR_PATH TO_STRING(BOR_SH_DIR) */
/* #define CONFIG_PATH TO_STRING(BOR_HOME) */
/* #define BROWSERS_LIST TO_STRING(BOR_BROWSERS) */
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <sys/types.h>

#define ERR_GOTO_ELSE(cond, gt, s)                                            \
    do {                                                                      \
        if (cond) {                                                           \
            goto gt;                                                          \
        } else {                                                              \
            s;                                                                \
        }                                                                     \
    } while (0)
#define ERR_SETERR_GOTO(cond, gt)                                             \
    do {                                                                      \
        if (cond) {                                                           \
            err = -1;                                                         \
            goto gt;                                                          \
        }                                                                     \
    } while (0)
#define ONERR_GOTO(var, gt)                                                   \
    do {                                                                      \
        if (var == 0 || var == NULL) {                                        \
            goto gt;                                                          \
        }                                                                     \
    } while (0)
#define SETERR_GOTO(run, gt)                                                  \
    do {                                                                      \
        if ((run) <= (-1)) {                                                  \
            err = -1;                                                         \
            log_print (LOG_ERROR, "In function '%s' <%s:%d>:  %s",            \
                       __FUNCTION__, __FILE__, __LINE__, strerror (errno));   \
            goto gt;                                                          \
        }                                                                     \
    } while (0)
#define NULLSETERR_GOTO(run, gt)                                              \
    do {                                                                      \
        if ((run) == NULL) {                                                  \
            err = -1;                                                         \
            log_print (LOG_ERROR, "%s <%s:%d>", strerror (errno), __FILE__,   \
                       __LINE__);                                             \
            goto gt;                                                          \
        }                                                                     \
    } while (0)

#define PRINTCWD()                                                            \
    char buf[PATH_MAX] = { 0 };                                               \
    getcwd (buf, PATH_MAX);                                                   \
    fprintf (stderr, ": %s\n", buf)

#define log_setlevel(level) LOG_LEVEL = level
#define BLOCK_SIZE 4096

// LOG_ERROR will always exit 1 process
enum LogLevel { LOG_ERROR, LOG_WARN, LOG_INFO, LOG_DEBUG };

extern enum LogLevel LOG_LEVEL;

// FUNCTIONS return 0 or NULL on failure, 1 on success
// check if env exists, else return default_str
int path_isvalid (const char *path, mode_t file_type);
char *str_merge (const char *format, ...); // make sure to free
void log_print (enum LogLevel level, const char *format, ...);
int array_hasstr (const char **array, size_t len, const char *target);
size_t forks_wait (pid_t *forks, size_t len);
const char *file_getext (const char *filename);
int cp_r (const char *oldpath, const char *newpath, int inc_root);

#endif
