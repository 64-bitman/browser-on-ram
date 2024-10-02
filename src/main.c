#include "main.h"
#include "mkdir_p.h"
#include "sync.h"
#include "util.h"
#include <assert.h>
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
        "BACKUPSDIR", "TARGETS",     "GSTATE_LEN" };

int main (int argc, char **argv) {
    int err = 0, opt = 0, fd = -1;
    int action = 0, overlay = 0;
    char *browsertype = NULL;

    (void)overlay;
    if (argc == 1) {
        printf ("Argument required\n");
        return 0;
    }

    LOG_LEVEL = LOG_INFO;
    while ((opt = getopt (argc, argv, "hsruovb:")) != -1) {
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
        case 'v':
            LOG_LEVEL = LOG_DEBUG;
            break;
        case 'b':
            browsertype = strdup (optarg);
            break;
        case 'h':
        default:
            printf ("Usage: browser-on-ram [-v] [-o] (-r | -u) <-b BROWSER> "
                    "[-s <TARGETS...>]\n");
            return 0;
        }
    }
    if (optind == argc && action == 's') {
        log_print (LOG_ERROR, "TARGETS is required");
    }

    char *prev_cwd = NULL, *argvbuf = NULL;
    struct passwd *pwp = getpwuid (getuid ());
    struct gitem *gstate = calloc (
        GSTATE_LEN, sizeof (struct gitem)); // init all pointers to NULL
    size_t len = 0, size = 0;

    if (browsertype == NULL) {
        log_print (LOG_ERROR,
                   "Option <-b BROWSER> must be supplied or an error occured");
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
    argvbuf = calloc (size + len, sizeof (*argvbuf));
    NULLSETERR_GOTO (argvbuf, exit);

    for (int i = optind; i < argc; i++) {
        strncat (argvbuf, argv[i], size - strlen (argvbuf) + 2);
        argvbuf[strlen (argvbuf)] = '\n';
    }
    gstate[TARGETS].str = argvbuf;
    argvbuf = NULL;

    // check env value first
    for (int i = 0; i < GSTATE_LEN; i++) {
        char buf[strlen (GState_str[i]) + strlen ("BOR_")];

        buf[0] = 0;
        strncat (buf, "BOR_", sizeof (buf) - 1);
        strncat (buf, GState_str[i], sizeof (buf) - 1);

        char *env = getenv (buf);

        if (env != NULL) {
            if (strcmp(buf, "BOR_TMPDIR") == 0) {
                gstate[i].str = str_merge("%s/%s/", env, gstate[BROWSERTYPE].str);
            } else {
                gstate[i].str = strdup(env);
            }
        }
    }

    // default values
    gstate[HOMEDIR].str = strdup (pwp->pw_dir);
    {
        char *bt = gstate[BROWSERTYPE].str;

        if (gstate[TMPDIR].str == NULL)
            gstate[TMPDIR].str
                = str_merge ("/run/user/%d/browser-on-ram/%s/", pwp->pw_uid, bt);

        if (gstate[CONFDIR].str == NULL)
            gstate[CONFDIR].str
                = str_merge ("%s/.config/browser-on-ram/", pwp->pw_dir);

        gstate[TARGETSDIR].str
            = str_merge ("%s/targets/%s", gstate[CONFDIR].str, bt);
        gstate[BACKUPSDIR].str
            = str_merge ("%s/backups/%s", gstate[CONFDIR].str, bt);
    }

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

        SETERR_GOTO (mkdir_p (gstate[TARGETSDIR].str), exit);
        SETERR_GOTO (mkdir_p (gstate[BACKUPSDIR].str), exit);

        SETERR_GOTO (mkdir_p (gstate[TMPDIR].str), exit);
    } else {
        free (prev_cwd);
        log_print (LOG_ERROR, "Failed creating directories");
        goto exit;
    }
    chdir (prev_cwd);

    if (action == 's') {
        log_print (LOG_INFO, "Starting sync");

        switch(sync_do (gstate)) {
            case -1:
                err = -1;
                goto exit;
                break;
            case -2:
                // deal with leftover files
                break;
            default:
                break;
        }
    } else if (action == 'r') {

    } else if (action == 'u') {
        log_print (LOG_INFO, "Starting unsync");

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
    free (argvbuf);

    return (err == -1) ? 1 : 0;
}
