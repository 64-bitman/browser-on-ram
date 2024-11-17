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
static char *TMPFSDIR = NULL;
static char *SCRIPTDIR = NULL;
static int IGNORE_CHECK = false;

struct Dir {
    char *dirname;
    char *path;
};

struct Browser {
    char *name;
    struct Dir *dirs;
    size_t dirs_len;
};

void help (void);
void status (void);
int set_lock (int status);

int initialize_dirs (void);
struct Dir create_dir_s (char **buffer, const char *browsername);

int do_action (int action);
int recover (const char *path, const char *browsername);

int sync_dir (const struct Dir dir, const char *browsername);
int unsync_dir (const struct Dir dir, const char *browsername);
int resync_dir (const struct Dir dir, const char *browsername);

int main (int argc, char **argv) {
    srand (time (NULL));
    errno = 0;
    struct stat sb;

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
        if (chdir (CONFDIR) == -1) return 1;
        int lock_exists = EXISTS ("lock");

        if (!IGNORE_CHECK) {
            if ((action == 'u' || action == 'r') && !lock_exists) {
                LOG (LOG_ERROR, "cannot unsync/resync, lock does not exist");
                return 1;
            } else if (action == 's' && lock_exists) {
                LOG (LOG_ERROR, "cannot sync, lock exists");
                return 1;
            }
        }

        if (action == 's') {
            set_lock (true);
        } else if (action == 'u') {
            set_lock (false);
        }

        if (do_action (action) == -1) {
            LOG (LOG_ERROR, "failed attempting to sync/unsync/resync");
            return 1;
        }
    }

    // status
    if (action == 'p') {
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
    printf ("-i, --ignore           ignore safety & lock checks\n");
    printf ("-v, --verbose          enable debug logs\n");
    printf ("-h, --help             show this message\n\n");
    printf ("It is not recommended to use sync, unsync, or resync standalone.\n");
    printf ("Please use the systemd user service instead\n");
}
// clang-format on

int set_lock (int status) {
    errno = 0;
    if (chdir (CONFDIR) == -1) return -1;

    if (status) {
        int fd = creat ("lock", O_RDONLY);

        if (fd == -1) return -1;
        fchmod (fd, 0444);
        close (fd);
    } else {
        chmod ("lock", 0666);
        errno = 0;
        if (unlink ("lock") == -1 && errno != ENOENT) return -1;
    }

    return 0;
}

void status (void) {}

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
        TMPFSDIR = print2string ("/run/user/%d/bor", pw->pw_uid);
    } else {
        TMPFSDIR = print2string ("%s/bor", xdgruntimedir);
    }

    SCRIPTDIR = strdup (SHAREDIR "/bor/scripts");

    if (CONFDIR == NULL || CONFDIR_BACKUPSDIR == NULL || TMPFSDIR == NULL
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
    if (mkdir_p (TMPFSDIR, 0755) == -1) return -1;

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

        char *filename = print2string ("%s.sh", browsername);

        if (!EXISTS (filename)) {
            LOG (LOG_WARN, "script for %s does not exist, excluding browser",
                 browsername);
            free (filename);
            continue;
        }

        free (filename);
        char *cmd = print2string ("exec sh ./%s.sh", browsername);
        if (cmd == NULL) return -1;

        char *buf = NULL;
        size_t dirpath_size = 0;
        FILE *pp = popen (cmd, "r");

        if (pp == NULL) return -1;

        struct Browser browser
            = { .name = strdup (browsername),
                .dirs = calloc (MAX_DIRS, sizeof (*(browser.dirs))),
                .dirs_len = 0 };
        if (browser.name == NULL) return -1;
        if (browser.dirs == NULL) return -1;

        LOG (LOG_DEBUG, "got browser %s", browser.name);

        // read from shell script output
        while (getline (&buf, &dirpath_size, pp) != -1) {
            buf = trim (buf);

            struct Dir dir
                = { .path = strdup (buf), .dirname = strdup (basename (buf)) };
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

int do_action (int action) {
    errno = 0;

    struct Browser *browsers = NULL;
    size_t browsers_len = 0;

    if (read_browsersconf (&browsers, &browsers_len) == -1) {
        LOG (LOG_ERROR, "failed reading browser.conf");
        return -1;
    }

    errno = 0;

    // check if browsers proccesses are running
    if (!IGNORE_CHECK) {
        for (size_t i = 0; i < browsers_len; i++) {
            if (pgrep (browsers[i].name) != -1 && action != 'r') {
                LOG (LOG_ERROR, "%s is running, aborting", browsers[i].name);
                return -1;
            }
        }
    }

    for (size_t b = 0; b < browsers_len; b++) {
        struct Browser browser = browsers[b];

        // create directories to store dirs for browser
        if (chdir (CONFDIR_BACKUPSDIR) == -1) continue;
        if (mkdir_p (browser.name, 0755) == -1) continue;
        if (chdir (TMPFSDIR) == -1) continue;
        if (mkdir_p (browser.name, 0755) == -1) continue;

        LOG (LOG_INFO, "syncing %s", browser.name);

        for (size_t d = 0; d < browser.dirs_len; d++) {
            struct Dir dir = browser.dirs[d];

            if (action == 's') {
                if (sync_dir (dir, browser.name) == -1) {
                    LOG (LOG_WARN, "failed syncing %s", dir.path);
                }
            } else if (action == 'u') {
                if (unsync_dir (dir, browser.name) == -1) {
                    LOG (LOG_WARN, "failed unsyncing %s", dir.path);
                    PERROR ();
                }
            } else if (action == 'r') {
                if (resync_dir (dir, browser.name) == -1) {
                    LOG (LOG_WARN, "failed resyncing %s", dir.path);
                    PERROR ();
                }
            }
        }
    }
    return 0;
}

int recover (const char *path, const char *browsername) {
    errno = 0;
    int err = 0;
    struct stat sb;

    if (stat (path, &sb) == -1) return -1;

    LOG (LOG_INFO, "recovering %s", path);

    // move path to crash directory
    char *_path = strdup (path);
    char *rlpath = realpath (path, NULL);
    char *prevcwd = get_current_dir_name ();

    if (_path == NULL || rlpath == NULL || prevcwd == NULL) {
        err = -1;
        goto exit;
    }

    if (chdir (CONFDIR_CRASHDIR) == -1 || mkdir_p (browsername, 0755) == -1
        || chdir (browsername) == -1) {
        err = -1;
        goto exit;
    }

    char *uniq_name
        = create_unique_filename (basename (_path), "-crashreport");

    errno = 0;
    if (move (rlpath, uniq_name) == -1) {
        LOG (LOG_ERROR, "could not move directory to crash dir");
        free (uniq_name);
        err = -1;
        goto exit;
    }

exit:
    chdir (prevcwd);
    free (_path);
    free (rlpath);
    free (prevcwd);
    return err;
}

int sync_dir (const struct Dir dir, const char *browsername) {
    errno = 0;
    struct stat sb;

    LOG (LOG_INFO, "syncing directory %s", dir.path);

    /*
       if directory doesn't exist, check if its in any other
       known directories and move it back if it does exist
       but there are other copies remaining, then recover
       those copies
    */

    if (chdir (CONFDIR_BACKUPSDIR) == -1) return -1;
    if (chdir (browsername) == -1) return -1;

    int backup_exists = DIREXISTS (dir.dirname);

    if (chdir (TMPFSDIR) == -1) return -1;
    if (chdir (browsername) == -1) return -1;

    if (DIREXISTS (dir.dirname)) {
        LOG (LOG_WARN, "found tmpfs copy of directory");

        // if backup copy exists too, then prioritize tmpfs copy over it
        if (EXISTS (dir.path) && !backup_exists) {
            if (recover (dir.dirname, browsername) == -1) {
                LOG (LOG_ERROR, "failed recovering tmpfs");
                return -1;
            }
        } else {
            LOG (LOG_INFO, "using tmpfs copy instead of original directory");

            errno = 0;
            if (remove (dir.path) == -1 && errno != ENOENT) return -1;
            ;
            if (move (dir.dirname, dir.path) == -1) return -1;
        }
    }
    remove (dir.dirname); // if dir.dirname is a file then delete it

    if (chdir (CONFDIR_BACKUPSDIR) == -1) return -1;
    if (chdir (browsername) == -1) return -1;

    if (DIREXISTS (dir.dirname)) {
        LOG (LOG_WARN, "found backup copy of directory");
        if (EXISTS (dir.path)) {
            if (recover (dir.dirname, browsername) == -1) {
                LOG (LOG_ERROR, "failed recovering backup");
                return -1;
            }
        } else {
            LOG (LOG_INFO,
                 "directory doesn't exist, using backup copy instead");

            if (remove (dir.path) == -1) return -1;
            ;
            if (move (dir.dirname, dir.path) == -1) return -1;
        }
    }
    remove (dir.dirname);

    /*
       1. copy directory to tmpfs
       2. move directory to backups
       3. symlink directory to tmpfs
    */

    if (chdir (TMPFSDIR) == -1) return -1;
    if (chdir (browsername) == -1) return -1;

    if (copy_r (dir.path, dir.dirname) == -1) {
        LOG (LOG_ERROR, "failed copying directory to tmpfs");
        return -1;
    }

    char *tmpfs_rlpath = realpath (dir.dirname, NULL);

    if (tmpfs_rlpath == NULL) return -1;

    if (chdir (CONFDIR_BACKUPSDIR) == -1 || chdir (browsername) == -1) {
        free (tmpfs_rlpath);
        return -1;
    }

    if (move (dir.path, dir.dirname) == -1) {
        LOG (LOG_ERROR, "failed moving directory to backups");
        free (tmpfs_rlpath);
        return -1;
    }

    if (symlink (tmpfs_rlpath, dir.path) == -1) {
        LOG (LOG_ERROR, "failed creating symlink");
        free (tmpfs_rlpath);
        return -1;
    }

    free (tmpfs_rlpath);
    return 0;
}

int unsync_dir (const struct Dir dir, const char *browsername) {
    errno = 0;
    struct stat sb;

    LOG (LOG_INFO, "unsyncing directory %s", dir.path);

    /*
       1. remove symlink
       2. copy tmpfs over to directory path
       3. remove backup
    */

    if (!SYMEXISTS (dir.path)) {
        if (LEXISTS (dir.path)) {
            LOG (LOG_WARN, "symlink is not actually a symlink");
        } else {
            LOG (LOG_WARN, "symlink does not exist");
        }
        return -1;
    }

    char *tmpfs_path = realpath (dir.path, NULL);

    if (tmpfs_path == NULL) return -1;

    if (remove (dir.path) == -1) {
        LOG (LOG_ERROR, "failed removing symlink");
        free (tmpfs_path);
        return -1;
    }
    if (move (tmpfs_path, dir.path) == -1) {
        LOG (LOG_ERROR, "failed moving tmpfs to directory location");
        free (tmpfs_path);
        return -1;
    }

    if (chdir (CONFDIR_BACKUPSDIR) == -1 || chdir (browsername) == -1) {
        free (tmpfs_path);
        return -1;
    }

    if (EXISTS (dir.dirname)) {
        if (remove_r (dir.dirname) == -1) {
            LOG (LOG_ERROR, "failed removing backup");
            free (tmpfs_path);
            return -1;
        }
    } else {
        LOG (LOG_WARN, "did not find backup copy of directory");
    }

    free (tmpfs_path);

    return 0;
}

int resync_dir (const struct Dir dir, const char *browsername) {
    errno = 0;

    LOG (LOG_INFO, "resyncing directory %s", dir.path);

    /*
        2. chdir to backups
        1. get realpath of symlink (tmpfs dir)
        3. copy tmpfs over
    */
    if (chdir (CONFDIR_BACKUPSDIR) == -1) return -1;
    if (chdir (browsername) == -1) return -1;

    char *tmpfs_path = realpath (dir.path, NULL);

    if (tmpfs_path == NULL) return -1;

    if (copy_r (tmpfs_path, dir.dirname) == -1) {
        LOG (LOG_ERROR, "failed syncing tmpfs to backups");
        free (tmpfs_path);
        return -1;
    }

    free (tmpfs_path);
    return 0;
}
