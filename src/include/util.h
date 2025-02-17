#pragma once

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/capability.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

// https://stackoverflow.com/a/12891181/12809652
#ifdef __GNUC__
#define UNUSED(x) UNUSED_##x __attribute__((__unused__))
#else
#define UNUSED(x) UNUSED_##x
#endif

#define EXISTS(path) (stat(path, &sb) == 0)
#define LEXISTS(path) (lstat(path, &sb) == 0)
#define SYMEXISTS(path) (lstat(path, &sb) == 0 && S_ISLNK(sb.st_mode))
#define DIREXISTS(path) (lstat(path, &sb) == 0 && S_ISDIR(sb.st_mode))
#define FEXISTS(path) (lstat(path, &sb) == 0 && S_ISREG(sb.st_mode))

#define STR_EQUAL(str1, str2) (strcmp(str1, str2) == 0)
#define TO__STRING(s) #s
#define TO_STRING(s) TO__STRING(s)

#define IS_DOT(fname) (STR_EQUAL(fname, ".") || STR_EQUAL(fname, ".."))

#define MAX_FD 1024

#define LOGCWD()                                       \
        do {                                           \
                char logcwd_cwd[PATH_MAX];             \
                getcwd(logcwd_cwd, PATH_MAX);          \
                plog(LOG_INFO, "CWD: %s", logcwd_cwd); \
        } while (0)

#define PERROR()                                                             \
        do {                                                                 \
                if (errno != 0) {                                            \
                        fprintf(stderr, "(%s:%d): %s\n", __FILE__, __LINE__, \
                                strerror(errno));                            \
                }                                                            \
        } while (0)

#define TRIM(buf, str)                                     \
        do {                                               \
                snprintf(buf, strlen(str) + 1, "%s", str); \
                trim(buf);                                 \
        } while (0)

enum LogLevel { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };

extern enum LogLevel LOG_LEVEL;

void plog(enum LogLevel level, const char *format, ...);
int create_dir(const char *path, mode_t mode);

char **search_path(size_t *len, const char *path, size_t count, ...);
void free_str_array(char **arr, size_t arr_len);
int trim(char *str);

int copy_path(const char *src, const char *dest, bool include_root);
int remove_dir(const char *path);
int remove_path(const char *path);

int move_path(const char *src, const char *dest, bool include_root);
int replace_paths(const char *target, const char *src);

void create_unique_path(char *buf, size_t buf_size, const char *path);
bool file_has_bad_perms(const char *path);

void set_caps(cap_flag_t set, cap_flag_value_t state, size_t count, ...);
bool check_caps_state(cap_flag_t set, cap_flag_value_t state, size_t count,
                      ...);

bool sd_uunit_active(const char *name);
pid_t get_pid(const char *name);
off_t get_dir_size(const char *path);
char *human_readable(off_t bytes);
bool program_exists(const char *program);
void update_string(char *str, size_t size, const char *input);

// from teeny-sha1.c
int sha1digest(uint8_t *digest, char *hexdigest, const uint8_t *data,
               size_t databytes);

// vim: sw=8 ts=8
