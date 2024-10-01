#include "util.h"
#include "mkdir_p.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <libgen.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum LogLevel LOG_LEVEL = LOG_DEBUG;

static const char *log_names[] = { "ERROR", "WARN", "INFO", "DEBUG" };

int path_isvalid (const char *path, mode_t file_type) {
    errno = 0;
    if (path == NULL) return 0;

    struct stat sb;

    if (lstat (path, &sb) != 0) return 0;
    if (file_type != 0) {
        if ((sb.st_mode & S_IFMT) != file_type) return 0;
    }
    return 1;
}

char *str_merge (const char *format, ...) {
    errno = 0;
    va_list args;

    // includes null byte
    va_start (args, format);
    size_t size = vsnprintf (NULL, 0, format, args) + 1;
    va_end (args);

    char *str = malloc (size * sizeof (*str));

    va_start (args, format);
    if (str != NULL) vsnprintf (str, size, format, args);
    va_end (args);

    return str;
}

void log_print (enum LogLevel level, const char *format, ...) {
    if (level > LOG_LEVEL) {
        return;
    }
    va_list args;

    va_start (args, format);

    fprintf (stderr, "(%s) ", log_names[level]);
    vfprintf (stderr, format, args);
    fprintf (stderr, "\n");

    va_end (args);
}

int array_hasstr (const char **array, size_t len, const char *target) {
    for (size_t i = 0; i < len; i++) {
        if (strcmp (target, array[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

// returns how many forks failed
size_t forks_wait (pid_t *forks, size_t len) {
    errno = 0;
    int err = 0, wstatus, exit_status;

    for (size_t i = 0; i < len; i++) {
        pid_t pid = waitpid (forks[i], &wstatus, 0);

        if (pid == -1) {
            err++;
        } else if (WIFEXITED (wstatus)) {
            exit_status = WEXITSTATUS (wstatus);

            err = (exit_status != 0) ? err + 1 : err;
        }
    }

    return err;
}

const char *file_getext (const char *filename) {
    const char *dot = strrchr (filename, '.');
    if (!dot || dot == filename) return "";
    return dot + 1;
}

int cp_r (const char *oldpath, const char *newpath) {
    errno = 0;
    int err = 0;

    char *old_path = NULL, *new_path = NULL;
    char *buffer = NULL;
    int fd_old = -1, fd_new = -1;
    struct stat *sb = NULL;
    FTS *ftsp = NULL;
    FTSENT *ent = NULL;

    buffer = calloc (PATH_MAX + 1, sizeof (*buffer));
    NULLSETERR_GOTO (buffer, exit);

    old_path = realpath (oldpath, NULL);
    NULLSETERR_GOTO (old_path, exit);

    new_path = realpath (newpath, NULL);
    NULLSETERR_GOTO (new_path, exit);

    char *paths[] = { old_path, NULL };
    int include_root = (oldpath[strlen (oldpath) - 1] == '/') ? 1 : 0;

    // to be initialized when fts_read returns root dir
    char *oldpath_basename = NULL; 

    ftsp = fts_open (paths, FTS_PHYSICAL | FTS_XDEV, NULL);
    NULLSETERR_GOTO (ftsp, exit);

    // recursively go through old_path dir
    while ((ent = fts_read (ftsp)) != NULL) {
        strncpy (buffer, new_path, PATH_MAX - 1);

        // add root dir if trailing slash in oldpath
        if (include_root && ent->fts_info != FTS_DP) {
            if (ent->fts_level == 0) {
                oldpath_basename = ent->fts_name;
            }
            if (strlen(buffer) != PATH_MAX) buffer[strlen(buffer)] = '/';
            strncat(buffer, oldpath_basename, PATH_MAX - strlen(buffer));
        }

        if (ent->fts_level != 0 ) {
            // cat everything after old_path root
            strncat (buffer, ent->fts_path + strlen (old_path),
                     PATH_MAX - strlen (buffer));
        }

        switch (ent->fts_info) {
        // check if dir
        case FTS_D:
            mkdir_p (buffer);
            continue;
        case FTS_DP:
            continue;
        }

        sb = ent->fts_statp;

        fd_old = open (ent->fts_path, O_RDONLY);
        SETERR_GOTO (fd_old, exit);

        fd_new = open (buffer, O_WRONLY | O_CREAT | O_TRUNC, sb->st_mode);
        SETERR_GOTO (fd_new, exit);

        ssize_t nread, nwritten;

        while ((nread = read (fd_old, buffer, PATH_MAX)), nread > 0) {
            do {
                char *outp = buffer;
                nwritten = write (fd_new, outp, nread);

                if (nwritten >= 0) {
                    nread -= nwritten;
                    outp += nwritten;
                } else if (nwritten != EINTR) {
                    SETERR_GOTO (-1, exit);
                }
            } while (nread > 0);
        }

        ERR_SETERR_GOTO (nread != 0, exit);

        close (fd_new);
        close (fd_old);
        fd_new = fd_old = -1;
    }

exit:
    if (fd_new >= 0) close (fd_new);
    if (fd_old >= 0) close (fd_old);
    fts_close (ftsp);
    free (old_path);
    free (new_path);
    free (buffer);
    return err;
}
