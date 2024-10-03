#include "sync.h"
#include "main.h"
#include "util.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int sync_do (struct gitem *gstate) {
    errno = 0;
    int err = 0;

    char *targetpath = strtok (gstate[TARGETS].str, "\n");
    char *targetbasename = NULL;
    char *prev_path = calloc (PATH_MAX, sizeof (*prev_path));
    int datafd = -1;

    NULLSETERR_GOTO (prev_path, exit);

    // write to datafp listing original target paths
    SETERR_GOTO (chdir (gstate[BACKUPSDIR].str), exit);
    datafd = creat ("./targets.txt", 0444);

    if (datafd == -1 && errno == EACCES) {
        log_print (LOG_WARN, "Found files of previous sync?");
        err = -2;
        goto exit;
    }
    SETERR_GOTO (datafd, exit);

    do {
        log_print (LOG_INFO, "Syncing '%s'", targetpath);
        targetbasename = str_merge ("./%s", basename (targetpath));

        // move targetpath to backups location
        SETERR_GOTO (chdir (gstate[BACKUPSDIR].str), exit);

        SETERR_GOTO (rename (targetpath, targetbasename), exit);
        NULLSETERR_GOTO (realpath (targetbasename, prev_path), exit);
        log_print (LOG_DEBUG, "Moved '%s' to '%s'", targetpath, prev_path);

        dprintf (datafd, "%s\n", targetpath);

        // copy targetpath (in backups location) to tmpdir
        SETERR_GOTO (chdir (prev_path), exit);
        SETERR_GOTO (cp_r (".", gstate[TMPDIR].str, 1), exit);
        SETERR_GOTO (chdir (gstate[TMPDIR].str), exit);
        NULLSETERR_GOTO (realpath (targetbasename, prev_path), exit);
        log_print (LOG_DEBUG, "Copied '%s/%s' to '%s'", gstate[BACKUPSDIR].str,
                   targetbasename + 2, prev_path);

        // symlink tmpdir target to original location
        SETERR_GOTO (chdir (dirname (targetpath)), exit);
        SETERR_GOTO (symlink (prev_path, targetbasename), exit);
        log_print (LOG_DEBUG, "Symlinked '%s' -> '%s/%s'", prev_path,
                   targetpath, targetbasename + 2);

        free (targetbasename);
        targetbasename = NULL;
        log_print (LOG_INFO, "Synced '%s'", targetpath);
    } while ((targetpath = strtok (NULL, "\n")) != NULL);

    log_print (LOG_INFO, "Sync successful");
exit:
    if (datafd != -1) close (datafd);
    free (targetbasename);
    free (prev_path);

    return err;
}

int unsync_do (struct gitem *gstate) {
    errno = 0;
    int err = 0;

    FILE *targetsfp = NULL;
    char *orig_path = NULL, *orig_name = NULL;
    size_t size = 0;

    SETERR_GOTO (chdir (gstate[BACKUPSDIR].str), exit);
    targetsfp = fopen ("./targets.txt", "r");

    if (targetsfp == NULL) {
        err = -1;
        if (errno == ENOENT) {
            log_print (LOG_WARN,
                       "Browser %s does not contain target.txt file?",
                       gstate[BROWSERTYPE].str);
            err = -2;
        }
        goto exit;
    }

    SETERR_GOTO (resync_do (gstate), exit);

    while (getline (&orig_path, &size, targetsfp) != -1) {
        orig_path[strlen (orig_path) - 1] = 0;
        log_print (LOG_DEBUG, "Found '%s'", orig_path);

        orig_name = strdup (basename (orig_path));
        NULLSETERR_GOTO (orig_name, exit);
        struct stat sb;

        if (stat (orig_name, &sb) == -1) {
            log_print (LOG_WARN, "Backup profile '%s' for %s does not exist",
                       orig_name, gstate[BROWSERTYPE].str);
            err = -2;
            goto exit;
        }

        // remove symlink target
        SETERR_GOTO (remove (orig_path), exit);
        SETERR_GOTO (rename (orig_name, orig_path), exit);
    }
    SETERR_GOTO (chdir (gstate[CONFDIR].str), exit);
    SETERR_GOTO (rmdir_r (gstate[BACKUPSDIR].str), exit);
    SETERR_GOTO (rmdir_r (gstate[TMPDIR].str), exit);
    SETERR_GOTO (rmdir_r (gstate[TARGETSDIR].str), exit);

exit:
    free (orig_path);
    free (orig_name);
    if (targetsfp != NULL) fclose (targetsfp);

    return err;
}

int resync_do (struct gitem *gstate) {
    errno = 0;
    int err = 0;

    char *paths[] = { gstate[TMPDIR].str, NULL };
    char *tmp_bkupname = NULL;
    FTS *ftsp = NULL;
    FTSENT *ent = NULL;

    ftsp = fts_open (paths, FTS_PHYSICAL | FTS_NOCHDIR, NULL);
    NULLSETERR_GOTO (ftsp, exit);

    SETERR_GOTO (chdir (gstate[BACKUPSDIR].str), exit);

    while ((ent = fts_read (ftsp)) != NULL) {

        if (ent->fts_info != FTS_D || ent->fts_level != 1) continue;
        fprintf (stderr, ": %s\n", ent->fts_path);
        struct stat sb;

        // check if matching backup and tmp
        if (stat (ent->fts_name, &sb) == -1) {
            log_print (LOG_WARN, "No matching backup for tmp profile '%s'?",
                       ent->fts_name);
            err = -2;
            goto exit;
        }
        // temporarily move backup to the side
        tmp_bkupname = str_merge("%s-BACKUP", ent->fts_path);
        SETERR_GOTO(rename(ent->fts_name, tmp_bkupname), exit); 

        // copy tmp to backup location
        if (cp_r(ent->fts_path, ent->fts_name, 0) == -1) {
            SETERR_GOTO(rename(tmp_bkupname, ent->fts_name), exit); 
            err = -1;
            goto exit;
        }
        rmdir_r(tmp_bkupname);
    }

exit:
    fts_close (ftsp);
    free(tmp_bkupname);

    return err;
}
