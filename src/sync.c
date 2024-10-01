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

    char *target = gstate[TARGETS].str, *newline = NULL;
    char *targetcpy = NULL, *backup_path = NULL, *tmp_path = NULL,
         *backup_symlink_path = NULL, *targetname = NULL;

    while ((newline = strchr (target, '\n'))) {
        if (!newline) break;
        *newline = 0;

        targetcpy = strdup (target);
        NULLSETERR_GOTO (targetcpy, exit);

        targetname = strdup (basename (targetcpy));
        NULLSETERR_GOTO (targetname, exit);

        // move target to backups
        backup_path = str_merge ("%s/%s", gstate[BACKUPSDIR].str, targetname);
        NULLSETERR_GOTO (backup_path, exit);

        log_print (LOG_DEBUG, "Moving %s -> %s", targetcpy, backup_path);
        SETERR_GOTO (rename (target, backup_path), exit);

        // copy backup to tmp diir
        tmp_path = strdup (target);

        tmp_path = str_merge ("%s/%s", gstate[TMPDIR].str, targetname);
        NULLSETERR_GOTO (tmp_path, exit);

        log_print (LOG_DEBUG, "Copying %s -> %s", backup_path, tmp_path);
        SETERR_GOTO (cp_r (backup_path, tmp_path), exit);

        // symlink original target path to tmpdir target
        log_print (LOG_DEBUG, "Symlinking %s -> %s", targetcpy, tmp_path);
        SETERR_GOTO (symlink (tmp_path, targetcpy), exit);

        // create a symlink to backup in original dir for targets
        backup_symlink_path = str_merge ("%s.bor-backup", targetcpy);

        log_print (LOG_DEBUG, "Symlinking %s -> %s", backup_symlink_path,
                   backup_path);
        SETERR_GOTO (symlink (backup_path, backup_symlink_path), exit);

        fprintf (stderr, "\n");
        *newline = '\n';
        target = newline + 1;
    }

exit:
    free (targetcpy);
    free (targetname);
    free (backup_path);
    free (tmp_path);
    free (backup_symlink_path);

    return err;
}

int unsync_do (struct gitem *gstate) {
    errno = 0;
    int err = 0;
    (void)gstate;

    return err;
}
