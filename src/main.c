#define _GNU_SOURCE
#include "util.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <getopt.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define SHAREDIR "/usr/local/share"
#define MAX_DIRS 1000
#define MAX_BROWSERS 100
#define VERSION "v1.0"

static char *HOMEDIR = NULL;
static char *CONFDIR = NULL;
static char *CONFDIR_BACKUPSDIR = NULL;
static char *CONFDIR_CRASHDIR = NULL;
static char *TMPDIR = NULL;
static char *SCRIPTDIR = NULL;
static int IGNORE_CHECK = false;

struct Dir {
    char *dirname;
    char *path;
    int exists;
};

struct Browser {
    char *name;
    struct Dir *dirs;
    size_t dirs_len;
};

void help (void);
/* void status (void); */
int toggle_lock (void);

int initialize_dirs (void);
struct Dir create_dir_s (char **buffer, const char *browsername);
int read_browsersconf (struct Browser **browsers, size_t *browsers_len);

int do_sync (struct Browser *browsers, size_t browsers_len);
/* int do_resync (void); */
/* int do_unsync (void); */

int main (int argc, char **argv) {
    srand (time (NULL));
    errno = 0;

    struct option opts[] = { { "sync", no_argument, NULL, 's' },
                             { "unsync", no_argument, NULL, 'u' },
                             { "resync", no_argument, NULL, 'r' },
                             { "status", no_argument, NULL, 'p' },
                             { "ignore", no_argument, NULL, 'i' },
                             { "verbose", no_argument, NULL, 'v' },
                             { "help", no_argument, NULL, 'h' },
                             { 0, 0, 0, 0 } };
    int opt;
    int longindex;
    char action = 0;

    while ((opt = getopt_long (argc, argv, "surpivh", opts, &longindex))
           != -1) {
        switch (opt) {
        case 's':
            action = 's';
            break;
        case 'u':
            action = 'u';
            break;
        case 'r':
            action = 'r';
            break;
        case 'v':
            LOG_LEVEL = LOG_DEBUG;
            break;
        case 'p':
            action = 'p';
            break;
        case 'i':
            IGNORE_CHECK = true;
            break;
        case 'h':
            help ();
            return 0;
        case '?':
            return 1;
        }
    }
    if (argc > optind || optind == 1) {
        help ();
        return 0;
    }

    if (initialize_dirs () == -1) {
        LOG (LOG_ERROR, "failed initializing directories");
        return 1;
    }

    int err = 0;

    if (action == 's' || action == 'r' || action == 'u') {
        // check if rsync is not available
        if (system ("which rsync > /dev/null 2>&1")) {
            LOG (LOG_ERROR, "could not find rsync, please install it");
            return 1;
        }

        // exit if systemd service is active
        if (systemd_userservice_active ("bor.service")) {
            LOG (LOG_ERROR, "Systemd user service is active, aborting");
            return 1;
        }
    }

    switch (action) {
    case 's': {
        struct Browser *browsers = NULL;
        size_t browsers_len = 0;

        if (read_browsersconf (&browsers, &browsers_len) == -1) {
            LOG (LOG_ERROR, "failed reading browser.conf");
            err = 1;
            break;
        }
        if (toggle_lock () == -1) return -1;
        err = do_sync (browsers, browsers_len) * -1;
        break;
    }
    case 'r':
        /* err = do_resync () * -1; */
        break;
    case 'u': {
        /* if (do_resync () == -1) { */
        /*     err = 1; */
        /*     break; */
        /* } */
        /* err = do_unsync () * -1; */
        if (toggle_lock () == -1) return -1;
        break;
    }
    case 'p':
        /* status (); */
        break;
    }
    return err;
}
// clang-format off
void help (void) {
    printf ("BROWSER-ON-RAM "VERSION"\n\n");
    printf ("Usage: bor [OPTION]\n");
    printf ("-s, --sync             sync browsers to memory\n");
    printf ("-u, --unsync           unsync browsers\n");
    printf ("-r, --resync           resync browsers\n");
    printf ("-p, --status           show current status and configuration\n");
    printf ("-i, --ignore           ignore safety checks\n");
    printf ("-v, --verbose          enable debug logs\n");
    printf ("-h, --help             show this message\n\n");
    printf ("It is not recommended to use sync, unsync, or resync standalone.\n");
    printf ("Please use the systemd user service instead\n");
}
// clang-format on

int toggle_lock (void) {
    errno = 0;
    if (chdir (CONFDIR) == -1) return -1;

    struct stat sb;

    if (EXISTS ("lock")) {
        // lock exists, remove it
        chmod ("lock", 0666);
        if (remove ("lock") == -1) return -1;
    } else {
        // create lock
        int fd = creat ("lock", O_RDONLY);
        fchmod (fd, 0444);
        close (fd);
    }

    return 0;
}

// init required dirs and create browsers.conf template
int initialize_dirs (void) {
    errno = 0;
    struct passwd *pw = getpwuid (getuid ());

    HOMEDIR = strdup (pw->pw_dir);

    if (HOMEDIR == NULL) {
        return -1;
    }
    // follow XDG base spec
    char *xdgconfighome = getenv ("XDG_CONFIG_HOME");

    if (xdgconfighome == NULL) {
        CONFDIR = print2string ("%s/.config/bor", HOMEDIR);
    } else {
        CONFDIR = print2string ("%s/bor", xdgconfighome);
    }
    CONFDIR_BACKUPSDIR = print2string ("%s/backups", CONFDIR);
    CONFDIR_CRASHDIR = print2string ("%s/crash-reports", CONFDIR);

    char *xdgruntimedir = getenv ("XDG_RUNTIME_DIR");

    if (xdgruntimedir == NULL) {
        TMPDIR = print2string ("/run/user/%d/bor", pw->pw_uid);
    } else {
        TMPDIR = print2string ("%s/bor", xdgruntimedir);
    }

    SCRIPTDIR = strdup (SHAREDIR "/bor/scripts");

    if (CONFDIR == NULL || CONFDIR_BACKUPSDIR == NULL || TMPDIR == NULL
        || SCRIPTDIR == NULL) {
        LOG (LOG_ERROR, "failed setting global vars");
        return -1;
    }

    LOG (LOG_DEBUG, "home directory is %s", HOMEDIR);
    LOG (LOG_DEBUG, "conf directory is %s", CONFDIR);
    LOG (LOG_DEBUG, "script directory is %s", SCRIPTDIR);

    struct stat sb;

    // share dir should be already created
    if (!EXISTS (SCRIPTDIR)) {
        LOG (LOG_ERROR, "script directory does not exist");
        return -1;
    }

    errno = 0;
    if (mkdir_p (CONFDIR, 0755) == -1) return -1;
    if (mkdir_p (CONFDIR_BACKUPSDIR, 0755) == -1) return -1;
    if (mkdir_p (CONFDIR_CRASHDIR, 0755) == -1) return -1;
    if (mkdir_p (TMPDIR, 0755) == -1) return -1;

    // create browser.conf template if it doesn't exist
    if (chdir (CONFDIR) == -1) return -1;

    if (!EXISTS ("browsers.conf")) {
        int fd = creat ("browsers.conf", 0644);

        dprintf (fd, "# each line corrosponds to a browser that should be "
                     "synced, ex:\n");
        dprintf (fd, "# firefox\n# chromium\n");

        close (fd);
    }

    return 0;
}

// read browsers.conf and return array of structs for dirs to synchronize
int read_browsersconf (struct Browser **browsers, size_t *browsers_len) {
    errno = 0;
    if (*browsers == NULL) {
        *browsers = calloc (MAX_BROWSERS, sizeof (**browsers));

        if (*browsers == NULL) return -1;
    }
    *browsers_len = 0;

    chdir (CONFDIR);

    struct stat sb;

    if (!EXISTS ("browsers.conf")) {
        LOG (LOG_ERROR, "browsers.conf does not exist");
        return -1;
    } else if (!S_ISREG (sb.st_mode) && !S_ISLNK (sb.st_mode)) {
        LOG (LOG_ERROR, "browsers.conf is not a file");
        return -1;
    }

    FILE *conf_fp = fopen ("browsers.conf", "r");

    if (conf_fp == NULL) {
        LOG (LOG_ERROR, "failed opening browsers.conf");
        return -1;
    }

    // read file
    char *browsername = NULL;
    size_t browsername_size = 0;

    while (getline (&browsername, &browsername_size, conf_fp) != -1) {
        browsername = trim (browsername);

        // ignore comments (#)
        if (browsername[0] == '#') continue;


        if (chdir (SCRIPTDIR) == -1) return -1;

        char *cmd = print2string ("exec sh ./%s.sh", browsername);
        if (cmd == NULL) return -1;

        char *buf = NULL;
        size_t dirpath_size = 0;
        FILE *pp = popen (cmd, "r");

        if (pp == NULL) return -1;

        struct Browser browser = { 
            .name = strdup (browsername),
            .dirs = calloc (MAX_DIRS, sizeof (*(browser.dirs))),
            .dirs_len = 0
        };
        if (browser.name == NULL) return -1;
        if (browser.dirs == NULL) return -1;

        LOG (LOG_DEBUG, "got browser %s", browser.name);

        // read from shell script output
        while (getline (&buf, &dirpath_size, pp) != -1) {
            buf = trim (buf);

            struct Dir dir = {
                .path = strdup(buf),
                .exists = (stat(buf,&sb) == 0) ? true : false,
                .dirname = strdup(basename(buf))
            };
            if (dir.path == NULL) return -1;
            if (dir.dirname == NULL) return -1;

            LOG (LOG_DEBUG, "received dir %s", dir.path);

            browser.dirs[browser.dirs_len] = dir;
            browser.dirs_len++;
        }

        (*browsers)[*browsers_len] = browser;
        (*browsers_len)++;

        free (cmd);
        free (buf);
        pclose (pp);
    }

    free (browsername);
    fclose (conf_fp);

    return 0;
}

int do_sync (struct Browser *browsers, size_t browsers_len) {
    errno = 0;
    LOG (LOG_INFO, "starting sync");

    // create browser dirs

    LOG(LOG_DEBUG, "%s", browsers[0].dirs[0].path);
    return 0;
}
