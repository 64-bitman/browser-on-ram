#define _GNU_SOURCE
#include "util.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fts.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static char *loglevels_str[] = { "ERROR", "WARN", "INFO", "DEBUG" };
int LOG_LEVEL = LOG_INFO;

void log_print (int level, const char *file, const int line,
                const char *format, ...) {
    if (level > LOG_LEVEL) {
        return;
    }

    va_list args;
    int size
        = snprintf (NULL, 0, "%s: %s:%d: ", loglevels_str[level], file, line);
    char buf[strlen (format) + size + 2];

    memset (buf, 0, sizeof (buf));
    snprintf (buf, sizeof (buf), "%s: %s:%d: ", loglevels_str[level], file,
              line);
    strcat (buf, format);
    buf[strlen (buf)] = '\n';

    va_start (args, format);

    vfprintf (stderr, buf, args);

    va_end (args);
}

char *print2string (const char *format, ...) {
    va_list args;
    char *str;

    va_start (args, format);
    if (vasprintf (&str, format, args) == -1) {
        str = NULL;
    }
    va_end (args);

    return str;
}

char *trim (char *str) {
    char *s = str;

    while (isspace (*s)) {
        s++;
    }

    char *e = s + strlen (s) - 1;

    while (isspace (*e)) {
        *e = 0;
        e--;
    }

    return s;
}

int copy_r (const char *src, const char *dest) {
    errno = 0;
    struct stat sb;

    if (stat (src, &sb) == -1) {
        return -1;
    }
    // add slash at end to prevent rsync placing src in dest
    char *cmd = print2string ("rsync -aX --no-whole-file --inplace '%s/' '%s'",
                              src, dest);

    if (cmd == NULL) return -1;

    int status = system (cmd);
    free (cmd);

    if (status == -1 || status != 0) {
        return -1;
    }

    return 0;
}

int remove_r (const char *path) {
    errno = 0;
    struct stat sb;

    if (stat (path, &sb) == -1) {
        return -1;
    }
    // if its not a dir just remove it
    if (!S_ISDIR (sb.st_mode)) {
        remove (path);
        return 0;
    }

    char *prevcwd = get_current_dir_name ();

    if (prevcwd == NULL) return -1;

    if (chdir (path) == -1) return -1;
    char *paths[] = { ".", NULL };

    FTS *ftsp = fts_open (paths, FTS_PHYSICAL | FTS_NOSTAT | FTS_XDEV, NULL);
    FTSENT *ent = NULL;

    while ((ent = fts_read (ftsp)) != NULL) {
        if (ent->fts_info == FTS_D || ent->fts_info == FTS_ERR) {
            continue;
        }
        chmod (ent->fts_name, 0755);
        remove (ent->fts_name);
    }

    fts_close (ftsp);
    if (chdir (prevcwd) == -1) {
        free (prevcwd);
        return -1;
    }
    free (prevcwd);

    chmod (path, 0755);
    if (remove (path) == -1) {
        return -1;
    }

    return 0;
}

// increment a number in given filename until filename is unique
char *create_unique_filename (const char *filename, const char *str) {
    char *new_filename = calloc (strlen (filename) + strlen (str) + 11,
                                 sizeof (*new_filename));
    struct stat sb;
    size_t n = 1;

    if (new_filename == NULL) return NULL;

    do {
        snprintf (new_filename, strlen (filename) + strlen (str) + 11,
                  "%s%s%ld", filename, str, n);
        n++;
    } while (stat (new_filename, &sb) == 0);

    return new_filename;
}

pid_t pgrep (const char *name) {
    DIR *dp = opendir ("/proc");
    struct dirent *ent;

    if (dp == NULL) return -1;
    char *exepath = calloc (PATH_MAX + 1, sizeof (*exepath));
    char *rlpath = calloc (PATH_MAX + 1, sizeof (*exepath));

    if (exepath == NULL || rlpath == NULL) return -1;

    while ((ent = readdir (dp)) != NULL) {
        long lpid = atol (ent->d_name);

        snprintf (exepath, PATH_MAX + 1, "/proc/%ld/exe", lpid);
        realpath (exepath, rlpath);

        if (rlpath != NULL) {
            if (strcmp (basename (rlpath), name) == 0) {
                free (exepath);
                free (rlpath);
                closedir (dp);
                return (pid_t)lpid;
            }
        }
    }

    free (exepath);
    free (rlpath);
    closedir (dp);
    return -1;
}

off_t get_dir_size (const char *path) {
    errno = 0;
    struct stat sb;

    if (stat (path, &sb) == -1) {
        return -1;
    }
    char *prevcwd = get_current_dir_name ();

    if (prevcwd == NULL) return -1;

    if (chdir (path) == -1) return -1;

    char *paths[] = { ".", NULL };

    FTS *ftsp = fts_open (paths, FTS_PHYSICAL | FTS_XDEV, NULL);
    FTSENT *ent = NULL;
    off_t size = 0;

    while ((ent = fts_read (ftsp)) != NULL) {
        if (ent->fts_info == FTS_F || ent->fts_info == FTS_NSOK
            || ent->fts_info == FTS_DEFAULT) {
            size += ent->fts_statp->st_size;
        }
    }

    fts_close (ftsp);
    if (chdir (prevcwd) == -1) {
        free (prevcwd);
        return -1;
    }
    free (prevcwd);

    return size;
}

char *human_readable (off_t bytes) {
    char *suffix[] = { "B", "KB", "MB", "GB", "TB" };
    char length = sizeof (suffix) / sizeof (suffix[0]);

    int i = 0;
    double dblBytes = bytes;

    if (bytes > 1024) {
        for (i = 0; (bytes / 1024) > 0 && i < length - 1; i++, bytes /= 1024)
            dblBytes = bytes / 1024.0;
    }

    char *str = NULL;
    asprintf (&str, "%.4g %s", dblBytes, suffix[i]);

    return str;
}

char *replace_char (char *str, char target, char replace) {
    char *current_pos = strchr (str, target);

    while (current_pos) {
        *current_pos = replace;
        current_pos = strchr (current_pos, target);
    }
    return str;
}

int systemd_userservice_active (const char *name) {
    char *cmd = print2string ("systemctl --user --quiet is-active '%s'", name);

    if (cmd == NULL) return false;

    int status = system (cmd);
    free (cmd);

    if (status == 0) {
        return true;
    }

    return false;
}

int mkdir_p (const char *path, mode_t mode) {
    errno = 0;
    char *path_cpy = strdup (path);

    if (path_cpy == NULL) return -1;

    char *mkdir_path = strchr (path_cpy, '/');

    while (mkdir_path != NULL) {
        char prev_char = mkdir_path[1];

        mkdir_path[1] = 0;

        if (mkdir (path_cpy, mode) == -1 && errno != EEXIST) {
            free (path_cpy);
            return -1;
        }

        mkdir_path[1] = prev_char;

        mkdir_path = strchr (mkdir_path + 1, '/');
    }

    if (mkdir (path_cpy, mode) == -1 && errno != EEXIST) {
        free (path_cpy);
        return -1;
    }
    free (path_cpy);

    return 0;
}

int move (const char *oldpath, const char *newpath) {
    errno = 0;
    struct stat sb;

    if (stat (newpath, &sb) == 0) {
        errno = EEXIST;
        return -1;
    }

    if (rename (oldpath, newpath) == -1) {
        if (errno != EXDEV) return -1;
    } else {
        return 0;
    }
    // if on different mount points, copy it then delete old
    if (copy_r (oldpath, newpath) == -1) return -1;
    if (remove_r (oldpath) == -1) return -1;
    return 0;
}
