#define _GNU_SOURCE
#include "sync.h"
#include "log.h"
#include "overlay.h"
#include "types.h"
#include "util.h"
#include "config.h"

#include <unistd.h>
#include <libgen.h>
#include <stdlib.h>
#include <fcntl.h>

#include <sys/types.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <stdbool.h>

static int sync_dir(struct Dir *dir, char *backup, char *tmpfs, bool overlay);
static int unsync_dir(struct Dir *dir, char *backup, char *tmpfs, char *otmpfs,
                      bool overlay);
static int resync_dir(struct Dir *dir, char *backup, char *tmpfs, char *otmpfs,
                      bool overlay);

#ifndef NOOVERLAY
static int repoint_dirs(const char *target);
#endif

static int repair_state(struct Dir *dir, char *backup, char *tmpfs,
                        bool overlay);
static int fix_session(struct Dir *dir, char *backup, char *tmpfs,
                       bool overlay);
static int fix_backup(char *backup, char *tmpfs);
static int fix_tmpfs(char *backup, char *tmpfs, bool overlay);

static int recover_path(struct Dir *syncdir, const char *path);

static int clear_cache(struct Dir *dir, const char *backup, const char *tmpfs);

static bool directory_is_safe(struct Dir *dir);

// perform action on directories of browser
int do_action_on_browser(struct Browser *browser, enum Action action,
                         bool overlay)
{
        plog(LOG_INFO, "doing '%s' on browser %s", action_str[action],
             browser->name);

        char backup[PATH_MAX], tmpfs[PATH_MAX], otmpfs[PATH_MAX];
        int did_something = 0;

        for (size_t i = 0; i < browser->dirs_num; i++) {
                struct Dir *dir = browser->dirs[i];
                int err = 0;

                if (!directory_is_safe(dir)) {
                        plog(LOG_WARN, "directory %s is unsafe, skipping",
                             dir->path);
                        continue;
                }

                // get required paths
                if (get_paths(dir, backup, tmpfs) == -1) {
                        plog(LOG_WARN, "failed getting required paths for %s",
                             dir->path);
                        continue;
                }
#ifndef NOOVERLAY
                if ((action == ACTION_UNSYNC || action == ACTION_RESYNC) &&
                    overlay && get_overlay_paths(dir, otmpfs) == -1) {
                        plog(LOG_WARN, "failed getting overlay path for %s",
                             dir->path);
                        continue;
                }
#endif

                // clear cache in tmpfs and backup
                if (action == ACTION_RMCACHE && dir->type == DIR_CACHE) {
                        if (clear_cache(dir, backup, tmpfs) == -1) {
                                plog(LOG_ERROR, "failed clearing cache for %s",
                                     dir->path);
                        }
                        continue;
                }

                // attempt to repair state if previous/current
                // sync session is corrupted
                if (repair_state(dir, backup, tmpfs, overlay) == -1) {
                        plog(LOG_WARN,
                             "failed checking state of previous sync session for %s",
                             dir->path);
                        continue;
                }

                // perform action
                if (action == ACTION_SYNC) {
                        err = sync_dir(dir, backup, tmpfs, overlay);
                } else if (action == ACTION_UNSYNC) {
                        err = unsync_dir(dir, backup, tmpfs, otmpfs, overlay);
                } else if (action == ACTION_RESYNC) {
                        err = resync_dir(dir, backup, tmpfs, otmpfs, overlay);
                }
                if (err == -1) {
                        plog(LOG_WARN, "failed %sing directory %s",
                             action_str[action], dir->path);
                        continue;
                }
                did_something++;
        }

        return (did_something > 0) ? 0 : -1;
}

// if overlay is true then don't copy to tmpfs
static int sync_dir(struct Dir *dir, char *backup, char *tmpfs, bool overlay)
{
        struct stat sb;

        if (SYMEXISTS(dir->path) && DIREXISTS(tmpfs) && DIREXISTS(backup)) {
                plog(LOG_INFO, "directory %s is already synced", dir->path);
                return 0;
        }
        if (!DIREXISTS(dir->path)) {
                plog(LOG_ERROR, "directory %s does not exist or is invalid",
                     dir->path);
                if (SYMEXISTS(dir->path)) {
                        plog(LOG_WARN,
                             "dangling symlink exists in its place, directory may have possibly been lost");
                }
                return -1;
        }

        // don't sync if cache dirs not enabled
        if (!CONFIG.enable_cache && dir->type == DIR_CACHE) {
                return 0;
        }
        bool did_something = false;

        plog(LOG_INFO, "syncing directory %s", dir->path);

        // if backup exists but dir doesnt, then move backup to dir location
        if (DIREXISTS(backup) && !DIREXISTS(dir->path)) {
                if (move_path(backup, dir->path, false) == -1) {
                        plog(LOG_ERROR, "failed moving backup to dir location");
                        PERROR();
                        return -1;
                }
        }

        // copy dir to tmpfs if we are not mounted (overlay)
        if (!overlay && !DIREXISTS(tmpfs)) {
                if (copy_path(dir->path, tmpfs, false) == -1) {
                        plog(LOG_ERROR, "failed syncing dir to tmpfs");
                        PERROR();
                        return -1;
                }
                did_something = true;
        }

        if (DIREXISTS(dir->path) && !LEXISTS(backup)) {
                // temporary path to swap with dir
                char tmp_path[PATH_MAX];

                create_unique_path(tmp_path, PATH_MAX, dir->path, 0);

                // create symlink
                if (symlink(tmpfs, tmp_path) == -1) {
                        plog(LOG_ERROR, "failed creating symlink");
                        PERROR();
                        return -1;
                }

                // swap atomically symlink and dir
                if (renameat2(AT_FDCWD, tmp_path, AT_FDCWD, dir->path,
                              RENAME_EXCHANGE) == -1) {
                        plog(LOG_ERROR, "failed swapping dir and symlink");
                        unlink(tmp_path);
                        PERROR();
                        return -1;
                }
                // move dir (tmp_path) to backup location
                if (move_path(tmp_path, backup, false) == -1) {
                        plog(LOG_ERROR, "failed moving dir to backups");
                        PERROR();
                        return -1;
                }
                // update tmpfs in case backup was modified after copy,
                // only if browser is running
                if (!overlay && get_pid(dir->browser->procname) >= 0) {
                        if (copy_path(backup, tmpfs, false) == -1) {
                                plog(LOG_ERROR,
                                     "failed syncing tmpfs with backup");
                                PERROR();
                                return -1;
                        }
                }
                did_something = true;
        }

        if (!did_something) {
                plog(LOG_INFO, "no sync action was performed");
        }

        return 0;
}

// automatically resyncs directory
static int unsync_dir(struct Dir *dir, char *backup, char *tmpfs, char *otmpfs,
                      bool overlay)
{
        struct stat sb;
        plog(LOG_INFO, "unsyncing directory %s", dir->path);

        if (EXISTSNOTSYM(dir->path)) {
                // not a symlink
                plog(LOG_INFO, "already unsynced");
                return 0;
        }
        if (DIREXISTS(tmpfs)) {
                // sync backup if tmpfs exists
                if (resync_dir(dir, backup, tmpfs, otmpfs, overlay) == -1) {
                        plog(LOG_ERROR, "failed resyncing");
                        return -1;
                }
        } else if (!DIREXISTS(backup)) {
                plog(LOG_ERROR, "backup nor tmpfs exists, cannot unsync");
                return -1;
        }
        if (replace_paths(dir->path, backup) == -1) {
                plog(LOG_ERROR, "failed to replace dir with backup");
                PERROR();
                return -1;
        }
        // update dir in case tmpfs was modified after copy,
        // only if browser is running
        if (DIREXISTS(tmpfs) && get_pid(dir->browser->procname) >= 0) {
                if (copy_path(tmpfs, dir->path, false) == -1) {
                        plog(LOG_ERROR, "failed syncing dir with tmpfs");
                        PERROR();
                        return -1;
                }
        }

        // we don't need to remove tmpfs if overlay is mounted
        // because it will disappear after unmount anyways
        if (!overlay && DIREXISTS(tmpfs) && remove_dir(tmpfs) == -1) {
                plog(LOG_ERROR, "failed removing tmpfs");
                PERROR();
                return -1;
        }

        return 0;
}

static int resync_dir(struct Dir *dir, char *backup, char *tmpfs, char *otmpfs,
                      bool overlay)
{
        if (!CONFIG.resync_cache && dir->type == DIR_CACHE) {
                return 0;
        }

        struct stat sb;

        plog(LOG_INFO, "resyncing directory %s", dir->path);

        if (!DIREXISTS(tmpfs)) {
                plog(LOG_ERROR, "%s does not exist", tmpfs);
                return -1;
        }
        // check if backup exists but not a directory
        if (EXISTSNOTDIR(backup)) {
                plog(LOG_ERROR, "%s is not a directory", backup);
                return -1;
        }
        char *tmp = (overlay) ? otmpfs : tmpfs;
        bool do_sync = true;

        // dont resync if otmpfs doesn't exist (means there arent any changes)
        if (overlay && !DIREXISTS(otmpfs)) {
                do_sync = false;
        } else {
                plog(LOG_DEBUG, "syncing tmpfs %s to backup", tmp);
        }

        if (do_sync && copy_path(tmp, backup, false) == -1) {
                plog(LOG_ERROR, "failed syncing %s with %s", tmpfs, backup);
                PERROR();
                return -1;
        }

        return 0;
}

#ifndef NOOVERLAY
// remount overlay in order to clear upper dir in an atomic way
// a normal resync should be done before this this
int reset_overlay(void)
{
        plog(LOG_INFO, "resetting overlay");

        if (!overlay_mounted()) {
                plog(LOG_WARN, "overlay not mounted");
                return -1;
        }

        if (repoint_dirs("backup") == -1) {
                plog(LOG_ERROR,
                     "failed repointing symlinks to respective backups");
                return -1;
        }

        if (unmount_overlay() == -1) {
                plog(LOG_ERROR, "failed unmounting directory");
                return -1;
        }

        if (mount_overlay() == -1) {
                plog(LOG_ERROR, "failed mounting directory");
                return -1;
        }

        if (repoint_dirs("tmpfs") == -1) {
                plog(LOG_ERROR,
                     "failed repointing symlinks to respective tmpfs'");
                return -1;
        }

        return 0;
}

// make all directory symlinks point to backup or tmpfs in an atomic way
static int repoint_dirs(const char *target)
{
        struct stat sb;

        char backup[PATH_MAX], tmpfs[PATH_MAX];
        const char *path = (strcmp(target, "tmpfs") == 0)  ? tmpfs :
                           (strcmp(target, "backup") == 0) ? backup :
                                                             NULL;

        if (path == NULL) {
                return -1;
        }

        for (size_t i = 0; i < CONFIG.browsers_num; i++) {
                struct Browser *browser = CONFIG.browsers[i];

                for (size_t k = 0; k < browser->dirs_num; k++) {
                        struct Dir *dir = browser->dirs[k];

                        // skip if path doesn't exist or is not a symlink
                        if (!LEXISTS(dir->path) || !S_ISLNK(sb.st_mode)) {
                                plog(LOG_WARN, "not resyncing directory %s",
                                     dir->path);
                                continue;
                        }
                        if (get_paths(dir, backup, tmpfs) == -1) {
                                plog(LOG_WARN,
                                     "failed getting required paths for %s",
                                     dir->path);
                                continue;
                        }

                        char tmp_path[PATH_MAX];

                        create_unique_path(tmp_path, PATH_MAX, dir->path, 0);

                        // create symlink
                        if (symlink(path, tmp_path) == -1) {
                                plog(LOG_WARN, "failed creating symlink %s",
                                     tmp_path);
                                PERROR();
                                continue;
                        }

                        // swap atomically symlink and new symlink
                        if (renameat2(AT_FDCWD, tmp_path, AT_FDCWD, dir->path,
                                      RENAME_EXCHANGE) == -1) {
                                plog(LOG_WARN,
                                     "failed swapping dir and symlink for %s",
                                     dir->path);
                                unlink(tmp_path);
                                PERROR();
                                continue;
                        }

                        // remove old symlink
                        if (unlink(tmp_path) == -1) {
                                plog(LOG_WARN, "failed removing %s", tmp_path);
                                PERROR();
                                continue;
                        }
                }
        }

        return 0;
}
#endif

// should be run before any action.
// repairs current session for directory or sends
// directories that are alone as recovery directories.
static int repair_state(struct Dir *dir, char *backup, char *tmpfs,
                        bool overlay)
{
        struct stat sb;

        if (DIREXISTS(dir->path)) {
                // if dir exists, then assume we aren't synced
                // any tmpfs or backup dirs are then converted into
                // recovery dirs
                if (recover_path(dir, backup) == -1 ||
                    recover_path(dir, tmpfs) == -1) {
                        plog(LOG_ERROR, "failed recovering directories");
                        return -1;
                }
        }
        if (fix_session(dir, backup, tmpfs, overlay) == -1) {
                plog(LOG_ERROR, "failed checking state");
                return -1;
        }

        return 0;
}

// attempt to fix session if the at least one directory exists.
static int fix_session(struct Dir *dir, char *backup, char *tmpfs, bool overlay)
{
        struct stat sb;

        if (fix_backup(backup, tmpfs) == -1 ||
            fix_tmpfs(backup, tmpfs, overlay) == -1) {
                plog(LOG_ERROR, "failed fixing directories");
                return -1;
        }

check:
        // create symlink if it doesn't exist
        if (DIREXISTS(tmpfs) && !LEXISTS(dir->path)) {
                plog(LOG_INFO, "symlink does not exist, creating it");

                if (symlink(tmpfs, dir->path) == -1) {
                        plog(LOG_ERROR, "failed creating symlink");
                        PERROR();
                        return -1;
                }
        } else if (lstat(dir->path, &sb) == 0 && !S_ISDIR(sb.st_mode) &&
                   !S_ISLNK(sb.st_mode)) {
                // dir is not a directory or symlink
                plog(LOG_ERROR, "dir is not a directory nor a symlink");
                return -1;
        } else if (SYMEXISTS(dir->path)) {
                // check if symlink points to correct path (tmpfs)
                char linkpath[PATH_MAX] = { 0 };

                if (readlink(dir->path, linkpath, PATH_MAX - 1) == -1) {
                        plog(LOG_ERROR, "failed reading link");
                        PERROR();
                        return -1;
                }
                if (!STR_EQUAL(linkpath, tmpfs)) {
                        plog(LOG_ERROR,
                             "symlink %s does not point to tmpfs, removing it",
                             dir->path);
                        if (unlink(dir->path) == -1) {
                                plog(LOG_ERROR, "failed removing symlink");
                                PERROR();
                                return -1;
                        }
                        // go back to create symlink
                        goto check;
                }
        }

        return 0;
}

static int fix_backup(char *backup, char *tmpfs)
{
        struct stat sb;
        // create backup by copying tmpfs
        if (DIREXISTS(tmpfs) && !DIREXISTS(backup)) {
                plog(LOG_INFO,
                     "backup not found, syncing tmpfs to backup location");

                if (copy_path(tmpfs, backup, false) == -1) {
                        plog(LOG_ERROR, "failed syncing tmpfs to backup");
                        PERROR();
                        return -1;
                }
        } else if (EXISTSNOTDIR(backup)) {
                // check if backup is not actually a directory
                plog(LOG_ERROR, "backup is not a directory");
                return -1;
        }
        return 0;
}

static int fix_tmpfs(char *backup, char *tmpfs, bool overlay)
{
        struct stat sb;

        // copy backup to tmpfs if it doesn't exist (only if no overlay)
        if (!overlay && DIREXISTS(backup) && !DIREXISTS(tmpfs)) {
                plog(LOG_INFO,
                     "tmpfs not found, syncing backup to tmpfs location");

                if (copy_path(backup, tmpfs, false) == -1) {
                        plog(LOG_ERROR, "failed syncing backup to tmpfs");
                        PERROR();
                        return -1;
                }
        } else if (EXISTSNOTDIR(tmpfs)) {
                // check if tmpfs is not a directory
                plog(LOG_ERROR, "tmpfs is not a directory");
                return -1;
        }
        return 0;
}

// if path exists, then move path to parent dir
// of syncdir and rename it as a recovery directory
static int recover_path(struct Dir *sync_dir, const char *path)
{
        struct stat sb;
        if (!LEXISTS(path)) {
                return 0;
        }
        time_t unixtime = time(NULL);
        struct tm *time_info = NULL;

        if (unixtime == (time_t)-1 ||
            (time_info = localtime(&unixtime)) == NULL) {
                plog(LOG_ERROR, "failed getting current time");
                PERROR();
                return -1;
        }
        plog(LOG_INFO, "recovering %s", path);

        // get parent directory path
        char *tmp = strdup(sync_dir->path);

        if (tmp == NULL) {
                PERROR();
                return -1;
        }
        char *parent_dir = dirname(tmp);

        if (STR_EQUAL(parent_dir, ".")) {
                plog(LOG_ERROR, "dir path returned '.'");
                free(tmp);
                return -1;
        }

        char recovery_path[PATH_MAX];
        char time_buf[100];

        if (strftime(time_buf, 100, "%d-%m-%y_%H:%M:%S", time_info) != 0) {
                snprintf(recovery_path, PATH_MAX,
                         "%s/" BOR_CRASH_PREFIX "%s_%s", parent_dir,
                         sync_dir->dirname, time_buf);
        } else {
                plog(LOG_ERROR, "time is empty");
                free(tmp);
                return -1;
        }
        free(tmp);

        char unique_path[PATH_MAX];

        create_unique_path(unique_path, PATH_MAX, recovery_path, 0);

        if (move_path(path, unique_path, false) == -1) {
                plog(LOG_ERROR, "failed moving dir to %s", unique_path);
                PERROR();
                return -1;
        }
        plog(LOG_INFO, "saved path as %s", unique_path);

        return 0;
}

static int clear_cache(struct Dir *dir, const char *backup, const char *tmpfs)
{
        struct stat sb;

        // remove tmpfs first before backup
        // reverse order seems to break overlay filesystem
        // (possibly to do with whiteout files?)
        if (DIREXISTS(tmpfs)) {
                plog(LOG_INFO, "clearing cache %s", tmpfs);
                if (clear_dir(tmpfs) == -1) {
                        PERROR();
                        return -1;
                }
        }

        if (DIREXISTS(backup)) {
                plog(LOG_INFO, "clearing cache %s", backup);
                if (clear_dir(backup) == -1) {
                        PERROR();
                        return -1;
                }
        }

        if (DIREXISTS(dir->path)) {
                plog(LOG_INFO, "clearing cache %s", dir->path);
                if (clear_dir(dir->path) == -1) {
                        PERROR();
                        return -1;
                }
        }

        return 0;
}

// write backup and tmpfs path for dir in given buffers
// buffers should be PATH_MAX in size
int get_paths(struct Dir *dir, char *backup, char *tmpfs)
{
        // generate hash from path of dir to prevent filename conflicts
        char hash[41] = { 0 };

        if (sha1digest(NULL, hash, (uint8_t *)dir->path, strlen(dir->path)) !=
            0) {
                PERROR();
                return -1;
        }

        plog(LOG_DEBUG, "using dirname %s_%s for %s", hash, dir->dirname,
             dir->path);

        snprintf(backup, PATH_MAX, "%s/%s_%s", PATHS.backups, hash,
                 dir->dirname);
        snprintf(tmpfs, PATH_MAX, "%s/%s_%s", PATHS.tmpfs, hash, dir->dirname);

        return 0;
}

#ifndef NOOVERLAY
// same as get_paths but only return tmpfs located in upper
int get_overlay_paths(struct Dir *dir, char *tmpfs)
{
        char hash[41] = { 0 };

        if (sha1digest(NULL, hash, (uint8_t *)dir->path, strlen(dir->path)) !=
            0) {
                PERROR();
                return -1;
        }

        plog(LOG_DEBUG, "using overlay dirname %s_%s for %s", hash,
             dir->dirname, dir->path);

        snprintf(tmpfs, PATH_MAX, "%s/%s_%s", PATHS.overlay_upper, hash,
                 dir->dirname);

        return 0;
}
#endif

// return true if directory and its parent directory is safe to handle
// safe means if file/dir is owned by user and if owner has read + write bits
static bool directory_is_safe(struct Dir *dir)
{
        struct stat sb;

        if (lstat(dir->path, &sb) == 0) {
                if (file_has_bad_perms(dir->path)) {
                        return false;
                }
        }

        if (file_has_bad_perms(dir->parent_path)) {
                return false;
        }

        return true;
}

// vim: sw=8 ts=8
