#define _GNU_SOURCE
#include "sync.h"
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
static int unsync_dir(struct Dir *dir, char *backup, char *tmpfs, bool overlay);
static int resync_dir(struct Dir *dir, char *backup, char *tmpfs);

static int repair_state(struct Dir *dir, char *backup, char *tmpfs);
static int fix_session(struct Dir *dir, char *backup, char *tmpfs);
static int fix_backup(char *backup, char *tmpfs);
static int fix_tmpfs(char *backup, char *tmpfs);

static int recover_path(struct Dir *syncdir, const char *path);

static bool directory_is_safe(struct Dir *dir);

// perform action on directories of browser
int do_action_on_browser(struct Browser *browser, enum Action action,
                         bool overlay)
{
        plog(LOG_INFO, "%sing browser %s", action_str[action], browser->name);

        char backup[PATH_MAX], tmpfs[PATH_MAX];

        for (size_t i = 0; i < browser->dirs_num; i++) {
                struct Dir *dir = browser->dirs[i];
                int err = 0;

                if (!directory_is_safe(dir)) {
                        plog(LOG_WARN, "directory %s is unsafe, skipping",
                             dir->path);
                        continue;
                }

                // find required paths
                if (get_paths(dir, backup, tmpfs) == -1) {
                        plog(LOG_WARN, "failed getting required paths for %s",
                             dir->path);
                        continue;
                }

                // attempt to repair state if previous/current
                // sync session is corrupted
                if (repair_state(dir, backup, tmpfs) == -1) {
                        plog(LOG_WARN,
                             "failed checking state of previous sync session for %s",
                             dir->path);
                        continue;
                }

                // perform action
                if (action == ACTION_SYNC) {
                        err = sync_dir(dir, backup, tmpfs, overlay);
                } else if (action == ACTION_UNSYNC) {
                        err = unsync_dir(dir, backup, tmpfs, overlay);
                } else if (action == ACTION_RESYNC) {
                        err = resync_dir(dir, backup, tmpfs);
                }
                if (err == -1) {
                        plog(LOG_WARN, "failed %sing directory %s",
                             action_str[action], dir->path);
                }
        }

        return 0;
}

// if overlay is true then don't copy to tmpfs
static int sync_dir(struct Dir *dir, char *backup, char *tmpfs, bool overlay)
{
        struct stat sb;

        if (!DIREXISTS(dir->path)) {
                plog(LOG_ERROR, "directory %s does not exist", dir->path);
                return -1;
        }

        // don't sync if cache dirs not enabled
        if (!CONFIG.enable_cache && dir->type == DIR_CACHE) {
                return 0;
        }
        bool did_something = false;

        plog(LOG_INFO, "syncing directory %s", dir->path);

        // copy dir to tmpfs if we are not mounted (overlay)
        if (!overlay && !DIREXISTS(tmpfs)) {
                if (copy_path(dir->path, tmpfs, false) == -1) {
                        plog(LOG_ERROR, "failed copying dir to tmpfs");
                        PERROR();
                        return -1;
                }
                did_something = true;
        }

        // move dir to backups
        if (!DIREXISTS(backup)) {
                if (move_path(dir->path, backup, false) == -1) {
                        PERROR();
                        plog(LOG_ERROR, "failed moving dir to backups");
                        return -1;
                }
                did_something = true;
        }

        // create symlink
        if (!SYMEXISTS(dir->path)) {
                if (symlink(tmpfs, dir->path) == -1) {
                        plog(LOG_ERROR, "failed creating symlink");
                        PERROR();

                        // move backup back
                        if (move_path(backup, dir->path, false) == -1) {
                                plog(LOG_WARN, "failed moving backup back");
                                PERROR();
                        }
                        return -1;
                }
                did_something = true;
        }

        if (!did_something) {
                plog(LOG_INFO, "no sync action was performed");
        }

        return 0;
}

// automatically resyncs directory
static int unsync_dir(struct Dir *dir, char *backup, char *tmpfs, bool overlay)
{
        struct stat sb;
        plog(LOG_INFO, "unsyncing directory %s", dir->path);

        if (LEXISTS(dir->path) && !SYMEXISTS(dir->path)) {
                // not a symlink
                plog(LOG_INFO, "already unsynced");
                return 0;
        }
        if (DIREXISTS(tmpfs)) {
                // sync backup if tmpfs exists
                plog(LOG_DEBUG, "copying tmpfs to backup");

                if (copy_path(tmpfs, backup, false) == -1) {
                        plog(LOG_ERROR,
                             "failed moving tmpfs back to symlink location");
                        PERROR();
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
        // we don't need to remove tmpfs if overlay is mounted
        // because it will disappear after unmount anyways
        if (!overlay && remove_dir(tmpfs) == -1) {
                plog(LOG_ERROR, "failed removing tmpfs");
                PERROR();
                return -1;
        }

        return 0;
}

static int resync_dir(struct Dir *dir, char *backup, char *tmpfs)
{
        if (!CONFIG.resync_cache && dir->type == DIR_CACHE) {
                return 0;
        }

        struct stat sb;

        plog(LOG_INFO, "resyncing directory %s", dir->path);
        if (!DIREXISTS(tmpfs)) {
                plog(LOG_ERROR, "tmpfs does not exist");
                return -1;
        }
        // check if backup exists but not a directory
        if (!DIREXISTS(backup) && LEXISTS(backup)) {
                plog(LOG_ERROR, "backup is not a directory");
                return -1;
        }
        if (copy_path(tmpfs, backup, false) == -1) {
                plog(LOG_ERROR, "failed syncing backup with tmpfs");
                PERROR();
                return -1;
        }

        return 0;
}

// should be run before any action.
// repairs current session for directory or sends
// directories that are alone as recovery directories.
static int repair_state(struct Dir *dir, char *backup, char *tmpfs)
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
        if (fix_session(dir, backup, tmpfs) == -1) {
                plog(LOG_ERROR, "failed checking state");
                return -1;
        }

        return 0;
}

// attempt to fix session if the at least one directory exists.
static int fix_session(struct Dir *dir, char *backup, char *tmpfs)
{
        struct stat sb;

        if (fix_backup(backup, tmpfs) == -1 || fix_tmpfs(backup, tmpfs) == -1) {
                plog(LOG_ERROR, "failed fixing directories");
                return -1;
        }

        // create symlink if it doesn't exist
        if (DIREXISTS(tmpfs) && !LEXISTS(dir->path)) {
                plog(LOG_INFO, "symlink does not exist, creating it");

                if (symlink(tmpfs, dir->path) == -1) {
                        plog(LOG_ERROR, "failed creating symlink");
                        PERROR();
                        return -1;
                }
        } else if (!DIREXISTS(dir->path) && !SYMEXISTS(dir->path) &&
                   LEXISTS(dir->path)) {
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
                     "backup not found, copying tmpfs to backup location");

                if (copy_path(tmpfs, backup, false) == -1) {
                        plog(LOG_ERROR, "failed copying tmpfs to backup");
                        PERROR();
                        return -1;
                }
        } else if (!DIREXISTS(backup)) {
                // check if backup is actually a file
                if (LEXISTS(backup)) {
                        plog(LOG_ERROR, "backup is a file");
                        return -1;
                }
        }
        return 0;
}

static int fix_tmpfs(char *backup, char *tmpfs)
{
        struct stat sb;

        // copy backup to tmpfs if it doesn't exist
        if (DIREXISTS(backup) && !DIREXISTS(tmpfs)) {
                plog(LOG_INFO,
                     "tmpfs not found, copying backup to tmpfs location");

                if (copy_path(backup, tmpfs, false) == -1) {
                        plog(LOG_ERROR, "failed copying backup to tmpfs");
                        PERROR();
                        return -1;
                }
        } else if (!DIREXISTS(tmpfs)) {
                // check if tmpfs is a file
                if (LEXISTS(tmpfs)) {
                        plog(LOG_ERROR, "tmpfs is a file");
                        return -1;
                }
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
                snprintf(recovery_path, PATH_MAX, "bor-crash_%s/%s_%s",
                         parent_dir, sync_dir->dirname, time_buf);
        } else {
                plog(LOG_ERROR, "time is empty");
                free(tmp);
                return -1;
        }
        free(tmp);

        char unique_path[PATH_MAX];

        create_unique_path(unique_path, PATH_MAX, recovery_path);

        if (move_path(path, unique_path, false) == -1) {
                plog(LOG_ERROR, "failed moving dir to %s", unique_path);
                PERROR();
                return -1;
        }
        plog(LOG_INFO, "saved path as %s", unique_path);

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

// vim: sw=8 ts=8

// return true if directory and its parent directory is safe to handle
static bool directory_is_safe(struct Dir *dir)
{
        char buf[PATH_MAX];

        snprintf(buf, PATH_MAX, "%s", dir->path);
        char *parent = dirname(buf);

        struct stat sb;

        if (lstat(dir->path, &sb) == 0) {
                if (file_has_bad_perms(dir->path)) {
                        return false;
                }
        }

        if (file_has_bad_perms(parent)) {
                return false;
        }

        return true;
}
