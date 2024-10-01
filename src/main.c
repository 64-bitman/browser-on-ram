#include "main.h"
#include "mkdir_p.h"
#include "sync.h"
#include "util.h"
#include <assert.h>
/* #include <errno.h> */
#include <dirent.h>
#include <fcntl.h>
#include <fts.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *const GState_str[]
    = { "HOMEDIR",     "TMPDIR",     "BROWSERTYPE", "CONFDIR",   "TARGETSDIR",
        "PROFILESDIR", "BACKUPSDIR", "TARGETS",     "GSTATE_LEN" };

int main (int argc, char **argv) {
    int err = 0, opt = 0, fd = -1;
    int action = 0, overlay = 0;
    char *browsertype = NULL;

    (void)overlay;
    if (argc == 1) {
        printf ("Argument required");
        return 0;
    }

    while ((opt = getopt (argc, argv, "hsruob:")) != -1) {
        switch (opt) {
        case 's':
            action = 's';
            break;
        case 'r':
            action = 'r';
            break;
        case 'u':
            action = 'u';
            break;
        case 'o':
            overlay = 1;
            break;
        case 'b':
            browsertype = strdup (optarg);
            break;
        case 'h':
        default:
            printf ("Usage: browser-on-ram [-o] (-s | -r | -u) <-b BROWSER> "
                    "TARGETS...\n");
            return 0;
        }
    }
    if (optind == argc && action == 's') {
        log_print (LOG_ERROR, "TARGETS is required");
    }

    char *prev_cwd = NULL, *buffer = NULL;
    struct passwd *pwp = getpwuid (getuid ());
    struct gitem *gstate = calloc (
        GSTATE_LEN, sizeof (struct gitem)); // init all pointers to NULL
    size_t len = 0, size = 0;

    if (browsertype == NULL) {
        log_print (LOG_ERROR,
                   "Option <-b BROWSER> must be supplied or error occured");
        goto exit;
    }
    gstate[BROWSERTYPE].str = browsertype;
    browsertype = NULL;

    // get sync targets from command line
    for (int i = optind; i < argc; i++) {
        size += strlen (argv[i]);
        len++;
    }
    size++;
    buffer = calloc (size + len, sizeof (*buffer));
    NULLSETERR_GOTO (buffer, exit);

    for (int i = optind; i < argc; i++) {
        strncat (buffer, argv[i], size - strlen (buffer) + 2);
        buffer[strlen (buffer)] = '\n';
    }
    gstate[TARGETS].str = buffer;
    buffer = NULL;

    // check env value first
    for (int i = 0; i < GSTATE_LEN; i++) {
        char buf[strlen (GState_str[i]) + strlen ("BOR_")];

        buf[0] = 0;
        strncat (buf, "BOR_", sizeof (buf) - 1);
        strncat (buf, GState_str[i], sizeof (buf) - 1);

        char *env = getenv (buf);

        if (env != NULL) {
            gstate[i].str = strdup (env);
        }
    }

    // default values
    gstate[HOMEDIR].str = strdup (pwp->pw_dir);

    if (gstate[TMPDIR].str == NULL)
        gstate[TMPDIR].str
            = str_merge ("/run/user/%d/browser-on-ram/", pwp->pw_uid);

    if (gstate[CONFDIR].str == NULL)
        gstate[CONFDIR].str
            = str_merge ("%s/.config/browser-on-ram/", pwp->pw_dir);

    gstate[TARGETSDIR].str = str_merge ("%s/targets/", gstate[CONFDIR].str);
    gstate[PROFILESDIR].str = str_merge ("%s/profiles/", gstate[CONFDIR].str);
    gstate[BACKUPSDIR].str = str_merge ("%s/backups/", gstate[CONFDIR].str);

    for (int i = 0; i < GSTATE_LEN; i++) {
        if (gstate[i].str == NULL) {
            log_print (LOG_ERROR, "'%s' cannot be '%s': Invalid value",
                       GState_str[i], gstate[i].str);
            goto exit;
        }
    }

    // create subdirectories
    prev_cwd = malloc ((PATH_MAX + 1) * sizeof (*prev_cwd));
    getcwd (prev_cwd, PATH_MAX + 1);

    if (prev_cwd != NULL) {
        SETERR_GOTO (mkdir_p (gstate[CONFDIR].str), exit);

        SETERR_GOTO (chdir (gstate[CONFDIR].str), exit);

        SETERR_GOTO (mkdir_p ("targets"), exit);
        SETERR_GOTO (mkdir_p ("profiles"), exit);
        SETERR_GOTO (mkdir_p ("backups"), exit);

        SETERR_GOTO (mkdir_p (gstate[TMPDIR].str), exit);
    } else {
        free (prev_cwd);
        log_print (LOG_ERROR, "Failed creating directories");
        goto exit;
    }
    chdir (prev_cwd);

    if (action == 's') {
        log_print (LOG_DEBUG, "Starting sync");

        SETERR_GOTO (sync_do (gstate), exit);
    } else if (action == 'r') {

    } else if (action == 'u') {
        log_print (LOG_DEBUG, "Starting unsync");

        SETERR_GOTO (unsync_do (gstate), exit);
    }

exit:
    if (err != 0) log_print (LOG_ERROR, "Something went wrong? ");

    for (int i = 0; i < GSTATE_LEN; i++) {
        free (gstate[i].str);
    }
    if (fd != -1) close (fd);
    free (gstate);
    free (prev_cwd);
    free (buffer);

    return (err == -1) ? 1 : 0;
}
