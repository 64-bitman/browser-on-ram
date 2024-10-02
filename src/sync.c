#include "sync.h"
#include "main.h"
#include "util.h"
#include <assert.h>
#include <errno.h>
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

    NULLSETERR_GOTO (prev_path, exit);

    do {
        log_print (LOG_INFO, "Syncing '%s'", targetpath);
        targetbasename = str_merge ("./%s", basename (targetpath));

        // move targetpath to backups location
        SETERR_GOTO (chdir (gstate[BACKUPSDIR].str), exit);
        SETERR_GOTO (rename (targetpath, targetbasename), exit);
        NULLSETERR_GOTO (realpath (targetbasename, prev_path), exit);
        log_print (LOG_DEBUG, "Moved '%s' to '%s'", targetpath, prev_path);

        // copy targetpath (in backups location) to tmpdir
        SETERR_GOTO (chdir (prev_path), exit);
        SETERR_GOTO (cp_r ("./", gstate[TMPDIR].str), exit);
        SETERR_GOTO (chdir (gstate[TMPDIR].str), exit);
        NULLSETERR_GOTO (realpath (targetbasename, prev_path), exit);
        log_print (LOG_DEBUG, "Copied '%s/%s' to '%s'", gstate[BACKUPSDIR].str,
                   targetbasename + 2, prev_path);

        // symlink tmpdir target to original location
        chdir (dirname (targetpath));
        SETERR_GOTO (symlink (prev_path, targetbasename), exit);
        log_print (LOG_DEBUG, "Symlinked '%s' -> '%s/%s'", prev_path,
                   targetpath, targetbasename + 2);

        free (targetbasename);
        targetbasename = NULL;
    } while ((targetpath = strtok (NULL, "\n")) != NULL);


    log_print (LOG_INFO, "Sync successful");
exit:
    free (targetbasename);
    free (prev_path);

    return err;
}

int unsync_do (struct gitem *gstate) {
    errno = 0;
    int err = 0;
    (void)gstate;

    return err;
}
