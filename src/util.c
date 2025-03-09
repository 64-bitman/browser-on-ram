#define _GNU_SOURCE
#include "util.h"

#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <libgen.h>
#include <unistd.h>
#include <glob.h>

#include <ctype.h>
#include <errno.h>
#include <sys/capability.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

enum LogLevel LOG_LEVEL = LOG_INFO;

static const char *log_str[] = { "DEBUG", "INFO", "WARN", "ERROR" };

void plog(enum LogLevel level, const char *format, ...)
{
        if (LOG_LEVEL > level) {
                return;
        }

        va_list args;

        va_start(args, format);

        fprintf(stderr, "%5s: ", log_str[level]);
        vfprintf(stderr, format, args);
        fprintf(stderr, "\n");

        va_end(args);
}

// essentially mkdir -p
int create_dir(const char *path, mode_t mode)
{
        struct stat sb;
        // chdir through each directory and
        // create the next, then chdir onto that one
        int err = 0;
        char prev_cwd[PATH_MAX], path_str[PATH_MAX];

        snprintf(path_str, PATH_MAX, "%s", path);
        if (getcwd(prev_cwd, PATH_MAX) == NULL) {
                err = -1;
                goto exit;
        }

        // chdir to root dir if absolute path
        if (path_str[0] == '/') {
                if (chdir("/") == -1) {
                        err = -1;
                        goto exit;
                }
        }

        char *dir = strtok(path_str, "/");

        while (dir != NULL) {
                if (EXISTS(dir))
                        goto finish_cycle;

                if (mkdir(dir, mode) == -1) {
                        err = -1;
                        goto exit;
                }

finish_cycle:
                if (chdir(dir) == -1) {
                        err = -1;
                        goto exit;
                }
                dir = strtok(NULL, "/");
        }

exit:
        if (chdir(prev_cwd) == -1) {
                return -1;
        }
        return err;
}

// find specified file/dir specified by path in given directories,
// return malloc'd array of malloc'd strings of size count and length len
// each string is an absolute path
char **search_path(size_t *len, const char *path, size_t count, ...)
{
        struct stat sb;

        int err = 0;
        char prev_cwd[PATH_MAX];
        char **array = malloc(count * sizeof(*array));
        va_list args;

        if (getcwd(prev_cwd, PATH_MAX) == NULL || array == NULL) {
                free(array);
                return NULL;
        }

        va_start(args, count);

        char *current_dir = NULL;
        (*len) = 0;

        for (size_t i = 0; i < count; i++) {
                current_dir = va_arg(args, char *);

                if (!DIREXISTS(current_dir) || chdir(current_dir) == -1) {
                        continue;
                }

                if (EXISTS(path)) {
                        char *unexpanded = NULL;

                        asprintf(&unexpanded, "%s/%s", current_dir, path);

                        array[*len] = realpath(unexpanded, NULL);
                        free(unexpanded);

                        if (array[*len] == NULL) {
                                err = -1;
                                break;
                        }
                        (*len)++;
                }

                if (chdir(prev_cwd) == -1) {
                        err = -1;
                        break;
                }
        }
        va_end(args);

        if (chdir(prev_cwd) == -1) {
                err = -1;
        }
        if (err == -1) {
                free_str_array(array, *len);
                free(array);
                return NULL;
        }
        return array;
}

// free array of strings up to arr_len not including the array
void free_str_array(char **arr, size_t arr_len)
{
        for (size_t i = 0; i < arr_len; i++) {
                free(arr[i]);
        }
}

// trim characters before and after the first or last non-whitespace chars
// modifies string in place
int trim(char *str)
{
        size_t start = 0, end = strlen(str) - 1;

        while (start < strlen(str) && isspace(str[start])) {
                start++;
        }
        while (end > start && isspace(str[end])) {
                end--;
        }
        char *copy = strdup(str);
        if (copy == NULL) {
                return -1;
        }

        snprintf(str, end - start + 2, "%s", copy + start);

        free(copy);

        return 0;
}

// copy directory using by forking off rsync
// if include_root param is true then make dest the
// parent directory of src when copying
int copy_path(const char *src, const char *dest, bool include_root)
{
        if (file_has_bad_perms(src)) {
                return -1;
        }

        char *cmdline = NULL;
        char *template = NULL;
        char *src_dup = strdup(src);

        if (src_dup == NULL) {
                return -1;
        }

        // remove trailing slash in src if there is one
        if (src_dup[strlen(src_dup) - 1] == '/') {
                src_dup[strlen(src_dup) - 1] = 0;
        }
        struct stat sb;
        if (stat(src_dup, &sb) == -1) {
                return -1;
        }

        // trailing clash indicates to only copy contents (only if directory)
        if (!include_root && S_ISDIR(sb.st_mode)) {
                template = "rsync -aAX  --no-whole-file --inplace '%s/' '%s'";
        } else {
                template = "rsync -aAX --no-whole-file --inplace '%s' '%s'";
        }

        if (asprintf(&cmdline, template, src_dup, dest) == -1) {
                free(src_dup);
                return -1;
        }
        free(src_dup);

        FILE *cmdp = popen(cmdline, "r");

        free(cmdline);

        if (cmdp == NULL || pclose(cmdp) != 0) {
                return -1;
        }

        return 0;
}

// handles fies/directories passed from nftw (3)
static int remove_dir_handler(const char *fpath, const struct stat *sb,
                              int UNUSED(typeflag), struct FTW *UNUSED(ftwbuf))
{
        // cannot chmod a symlink
        if (!S_ISLNK(sb->st_mode) && chmod(fpath, S_IWUSR) == -1) {
                return -1;
        } else if (remove(fpath) == -1) {
                return -1;
        }

        return 0;
}

int remove_dir(const char *path)
{
        if (file_has_bad_perms(path)) {
                return -1;
        }

        struct stat sb;

        if (!DIREXISTS(path)) {
                return -1;
        }

        if (nftw(path, remove_dir_handler, MAX_FD, FTW_DEPTH | FTW_PHYS) ==
            -1) {
                return -1;
        }

        return 0;
}

int remove_path(const char *path)
{
        // try using remove() and then remove_dir if path is not empty
        errno = 0;
        if (remove(path) == -1) {
                int err = 0;
                if (errno == ENOTEMPTY) {
                        err = remove_dir(path);
                }
                if (errno != EISDIR && (errno != 0 || err == -1)) {
                        return -1;
                }
        }
        return 0;
}

// remove dir contents
int clear_dir(const char *path)
{
        glob_t gb;

        char pattern[PATH_MAX];

        snprintf(pattern, PATH_MAX, "%s/*", path);

        int err = glob(pattern, GLOB_NOSORT, NULL, &gb);

        if (err != 0 && err != GLOB_NOMATCH) {
                globfree(&gb);
                return -1;
        }

        for (size_t i = 0; i < gb.gl_pathc; i++) {
                if (remove_path(gb.gl_pathv[i]) == -1) {
                        err = -1;
                }
        }

        globfree(&gb);

        return err;
}

// move src to dest inplace via rename (2) if on same filesystem
// else copy it to dest and remove src
// include_root -> see copy_dir()
int move_path(const char *src, const char *dest, bool include_root)
{
        if (file_has_bad_perms(src)) {
                return -1;
        }

        char *dest_dup = NULL;

        if (include_root) {
                if (mkdir(dest, 0755) == -1 && errno != EEXIST) {
                        return -1;
                }

                char *tmp = strdup(src);
                asprintf(&dest_dup, "%s/%s", dest, basename(tmp));
                free(tmp);

                if (dest_dup == NULL) {
                        return -1;
                }
        } else {
                dest_dup = (char *)dest;
        }

        // attempt to use rename(), if returns EXDEV errno then do copy method
        errno = 0;
        if (rename(src, dest_dup) == -1) {
                if (errno == EXDEV) {
                        if (copy_path(src, dest_dup, false) == -1) {
                                return -1;
                        }
                        if (remove_dir(src) == -1) {
                                return -1;
                        }
                        return 0;
                }
                return -1;
        }

        return 0;
}

// replace target with src atomically, works over different filesystems
// deletes target after its been swapped
int replace_paths(const char *target, const char *src)
{
        char beside_path[PATH_MAX];

        create_unique_path(beside_path, PATH_MAX, target);

        if (move_path(src, beside_path, false) == -1) {
                return -1;
        }
        if (renameat2(AT_FDCWD, beside_path, AT_FDCWD, target,
                      RENAME_EXCHANGE) == -1) {
                return -1;
        }
        if (remove_path(beside_path) == -1) {
                return -1;
        }

        return 0;
}

// iterate over a number until there is a unused filename
// in format of <path>-<number>
// will null terminate buf and preserve errno
void create_unique_path(char *buf, size_t buf_size, const char *path)
{
        int prev_errno = errno;
        struct stat sb;

        snprintf(buf, buf_size, "%s", path);

        if (EXISTS(buf)) {
                size_t i = 0;

                while (EXISTS(buf) && i <= UNIQUE_PATH_MAX_ITER) {
                        i++;
                        snprintf(buf, PATH_MAX, "%s-%ld", path, i);
                }
        }
        errno = prev_errno;
}

// return true if file/dir is not owned by user or
// if owner does not have read + write bits
bool file_has_bad_perms(const char *path)
{
        struct stat sb;

        if (lstat(path, &sb) == -1) {
                return true;
        } else {
                if (sb.st_uid != getuid() || (sb.st_mode & 0777) < 0600) {
                        return true;
                }
        }
        return false;
}

#ifndef NOOVERLAY

// set the state of given capabiltiies in set and exit program on failure
void set_caps(cap_flag_t set, cap_flag_value_t state, size_t count, ...)
{
        cap_value_t caps[cap_max_bits()];
        cap_value_t current_cap;
        va_list args;

        va_start(args, count);

        for (size_t i = 0; i < count; i++) {
                current_cap = va_arg(args, cap_value_t);

                if (!CAP_IS_SUPPORTED(current_cap)) {
                        goto error;
                }

                caps[i] = current_cap;
        }

        va_end(args);

        cap_t caps_state = cap_get_proc();

        if (caps_state == NULL) {
                goto error;
        }

        if (cap_set_flag(caps_state, set, (int)count, caps, state) == -1) {
                goto error;
        }
        if (cap_set_proc(caps_state) == -1) {
                goto error;
        }

        cap_free(caps_state);
        return;
error:
        perror("failed setting capability");
        exit(1);
}

// return true if specified capabilities are in state in given set
// exit program on failure
bool check_caps_state(cap_flag_t set, cap_flag_value_t state, size_t count, ...)
{
        cap_t caps_state = cap_get_proc();

        if (caps_state == NULL) {
                goto error;
        }

        va_list args;
        cap_flag_value_t cstate;
        cap_value_t current_cap;
        bool bad = false;

        va_start(args, count);

        for (size_t i = 0; i < count; i++) {
                current_cap = va_arg(args, cap_value_t);

                if (!CAP_IS_SUPPORTED(current_cap)) {
                        bad = true;
                        break;
                }

                if (cap_get_flag(caps_state, current_cap, set, &cstate) == -1) {
                        goto error;
                }
                if (cstate != state) {
                        bad = true;
                        break;
                }
        }

        va_end(args);
        cap_free(caps_state);

        return (bad) ? false : true;
error:
        perror("failed checking capabilities");
        exit(1);
}

#endif

#ifndef NOSYSTEMD
// check if systemd user unit is active
bool sd_uunit_active(const char *name)
{
        char *cmd = NULL;

        asprintf(&cmd, "systemctl --user --quiet is-active '%s'", name);

        if (cmd == NULL) {
                return false;
        }

        int status = system(cmd);
        free(cmd);

        if (status == 0) {
                return true;
        }

        return false;
}
#endif

// returns pid if name is found, else return -1
pid_t get_pid(const char *name)
{
        DIR *dp = opendir("/proc");
        struct dirent *ent;

        if (dp == NULL) {
                return -1;
        }
        char exepath[PATH_MAX];
        char rlpath[PATH_MAX];

        while (errno = 0, (ent = readdir(dp)) != NULL) {
                long lpid = atol(ent->d_name);

                snprintf(exepath, PATH_MAX, "/proc/%ld/exe", lpid);

                if (realpath(exepath, rlpath) != NULL) {
                        if (strcmp(basename(rlpath), name) == 0) {
                                closedir(dp);
                                return (pid_t)lpid;
                        }
                }
        }
        closedir(dp);
        return -1;
}

static off_t dir_size = 0;

static int get_dir_size_handler(const char *UNUSED(fpath),
                                const struct stat *sb, int typeflag,
                                struct FTW *UNUSED(ftwbuf))
{
        if (typeflag == FTW_F) {
                dir_size += sb->st_size;
        }
        return 0;
}

// get dir size in bytes
off_t get_dir_size(const char *path)
{
        dir_size = 0;

        if (nftw(path, get_dir_size_handler, MAX_FD, 0) == -1) {
                return -1;
        }

        return dir_size;
}

// convert size in bytes to malloc'd string in human readable format
char *human_readable(off_t bytes)
{
        if (bytes < 0) {
                char *str = NULL;
                asprintf(&str, "UNKNOWN");
                return str;
        }

        char *suffix[] = { "B", "KB", "MB", "GB", "TB" };
        char length = sizeof(suffix) / sizeof(suffix[0]);

        int i = 0;
        double dblBytes = (double)bytes;

        if (bytes > 1024) {
                for (i = 0; (bytes / 1024) > 0 && i < length - 1;
                     i++, bytes /= 1024)
                        dblBytes = (double)bytes / 1024.0;
        }

        char *str = NULL;
        asprintf(&str, "%.4g %s", dblBytes, suffix[i]);

        return str;
}

// check if program exists in $PATH
bool program_exists(const char *program)
{
        char cmdline[strlen(program) + 100];

        snprintf(cmdline, strlen(program) + 100,
                 "command -v %s > /dev/null 2>&1", program);

        int exit = system(cmdline);

        if (exit == 0) {
                return true;
        }

        return false;
}

// only update str if input is not NULL or empty
void update_string(char *str, size_t size, const char *input)
{
        if (input != NULL && strlen(input) > 0) {
                snprintf(str, size, "%s", input);
        }
}

// check if name is . or ..
bool name_is_dot(const char *name)
{
        if (STR_EQUAL(name, ".") || STR_EQUAL(name, "..")) {
                return true;
        }
        return false;
}

// vim: sw=8 ts=8
