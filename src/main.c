#define _GNU_SOURCE
#include "util.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <getopt.h>
#include <libgen.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef SHAREDIR
    #define SHAREDIR "/usr/share"
#endif

#ifndef MAX_DIRS
    #define MAX_DIRS 100
#endif

#ifndef MAX_BROWSERS
    #define MAX_BROWSERS 100
#endif

#ifndef VERSION
    #define VERSION "v1.0"
#endif

static char *HOMEDIR = NULL;
static char *CONFDIR = NULL;
static char *CONFDIR_BACKUPSDIR = NULL;
static char *CONFDIR_CRASHDIR = NULL;
static char *TMPFSDIR = NULL;
static char *SCRIPTDIR = NULL;
static int IGNORE_CHECK = false;
static uid_t USERID = 0;
static gid_t GROUPID = 0;

enum DirType { TYPE_PROFILE = 1, TYPE_CACHE, TYPES_LEN };

static const char *DirType_str[] = { NULL, "profile", "cache" };

struct Dir {
    char *dirname;
    char *path;
    enum DirType type;
};

struct Browser {
    char *name;
    struct Dir *dirs;
    size_t dirs_len;
};

void help (void);
void status (void);
int set_lock (int status);
int read_browsersconf (struct Browser **browsers, size_t *browsers_len);

int init (void);

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
                             { "config", required_argument, NULL, 'c' },
                             { "sharedir", required_argument, NULL, 'd' },
                             { "tmpfs", required_argument, NULL, 't' },
                             { "verbose", no_argument, NULL, 'v' },
                             { "help", no_argument, NULL, 'h' },
                             { 0, 0, 0, 0 } };
    int opt;
    int longindex;
    char action = 0;

    while ((opt = getopt_long (argc, argv, "surpic:d:t:vh", opts, &longindex))
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
        case 'c': {
            // set config directory path
            char *tmp = strdup (optarg);
            if (tmp == NULL) return 1;

            // only get realpath of dirname of path, incase
            // path doesn't exist
            char *bn = basename (tmp);
            char *dn_rlpath = realpath (dirname (optarg), NULL);

            if (dn_rlpath == NULL) return 1;
            CONFDIR = print2string ("%s/%s", dn_rlpath, bn);

            free (tmp);
            free (dn_rlpath);

            if (CONFDIR == NULL) {
                PERROR ();
                return 1;
            }
            break;
        }
        case 'd': {
            char *share_rlpath = realpath (optarg, NULL);
            if (share_rlpath == NULL) return 1;

            SCRIPTDIR = print2string ("%s/scripts", share_rlpath);

            if (SCRIPTDIR == NULL) {
                PERROR ();
                return 1;
            }
            break;
        }
        case 't': {
            TMPFSDIR = realpath (optarg, NULL);

            if (TMPFSDIR == NULL) {
                PERROR ();
                return 1;
            }
            break;
        }
        case 'h':

            help ();
            return 0;
        case '?':
            return 1;
        default:
            LOG (LOG_ERROR, "getopt error");
            return 1;
        }
    }
    if (argc > optind || optind == 1) {
        help ();
        return 0;
    }

    if (init () == -1) {
        LOG (LOG_ERROR, "failed initializing");
        PERROR ();
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
        if (systemd_userservice_active ("bor.service") && action != 'r' && !IGNORE_CHECK) {
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

        if (do_action (action) == -1) {
            LOG (LOG_ERROR, "failed attempting to sync/unsync/resync");
            return 1;
        }

        if (action == 's') {
            set_lock (true);
        } else if (action == 'u') {
            set_lock (false);
        }
    }

    // status
    if (action == 'p') {
        status ();
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
    printf ("-c, --config           override config directory location\n");
    printf ("-d, --sharedir         override data/share directory location\n");
    printf ("-t, --tmpfs            override tmpfs directory location\n");
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

void status (void) {
    errno = 0;
    struct stat sb;

    struct Browser *browsers = NULL;
    size_t browsers_len = 0;

    if (read_browsersconf (&browsers, &browsers_len) == -1) return;
    if (chdir (CONFDIR) == -1) return; // for lock

    printf ("Browser-on-RAM " VERSION "\n\n");

    char *srv_active, *timer_active;

    srv_active = systemd_userservice_active ("bor.service") ? "true" : "false";
    timer_active = systemd_userservice_active ("bor-resync.timer") ? "true" : "false";

    char *lock_exists = EXISTS ("lock") ? "true" : "false";

    printf ("%-20s%s\n", "Active:", lock_exists);
    printf ("%-20s%s\n", "Systemd service:", srv_active);
    printf ("%-20s%s\n", "Systemd timer:", timer_active);

    printf ("\nConfigured directories\n\n");

    // impletement status
    char *buf = calloc (PATH_MAX + 1, sizeof (*buf));

    if (buf == NULL) return;

    for (size_t b = 0; b < browsers_len; b++) {

        struct Browser browser = browsers[b];

        printf ("%c%s:\n\n", toupper (browser.name[0]), browser.name + 1);

        for (size_t d = 0; d < browser.dirs_len; d++) {
            struct Dir dir = browser.dirs[d];
            off_t dir_size = get_dir_size (dir.path);

            printf ("%-20s%s\n", "Type:", DirType_str[dir.type]);

            if (dir_size != -1) {
                printf ("%-20s%s\n", "Directory:", dir.path);
            } else {
                printf ("%-20s%s %s\n", "Directory:", dir.path,
                        "(DOES NOT EXIST!)");
            }

            // get tmpfs path
            snprintf (buf, PATH_MAX + 1, "%s/%s/%s", TMPFSDIR, browser.name,
                      dir.dirname);

            if (stat (buf, &sb) == 0) {
                printf ("%-20s%s\n", "Tmpfs:", buf);
            }

            if (dir_size != -1) {
                char *human = human_readable (dir_size);
                printf ("%-20s%s\n", "Directory size:", human);
                free (human);
            }

            // get any crash recovery directories
            snprintf (buf, PATH_MAX + 1, "%s/%s", CONFDIR_CRASHDIR,
                      browser.name);
            DIR *dp = opendir (buf);

            if (dp == NULL) return;

            struct dirent *de = NULL;

            while ((de = readdir (dp)) != NULL) {
                if (de->d_type == DT_DIR) {
                    char *str_start = strstr (de->d_name, "-crashreport");

                    // remove -crashreport* substring
                    if (str_start == NULL) continue;
                    char prevc = *str_start;
                    *str_start = 0;

                    if (strcmp (de->d_name, dir.dirname) != 0) continue;

                    *str_start = prevc;
                    printf ("%-20s%s/%s\n", "Crash directory:", buf,
                            de->d_name);
                }
            }
            printf ("\n");

            closedir (dp);
        }
    }
    free (buf);
}

// init required dirs and create browsers.conf template
int init (void) {
    errno = 0;
    struct passwd *pw = getpwuid (getuid ());

    HOMEDIR = strdup (pw->pw_dir);
    USERID = pw->pw_uid;
    GROUPID = pw->pw_gid;

    if (HOMEDIR == NULL) {
        return -1;
    }

    // only set confdir if it wasnt given by user
    if (CONFDIR == NULL) {
        // follow XDG base spec
        char *xdgconfighome = getenv ("XDG_CONFIG_HOME");

        if (xdgconfighome == NULL) {
            CONFDIR = print2string ("%s/.config/bor", HOMEDIR);
        } else {
            CONFDIR = print2string ("%s/bor", xdgconfighome);
        }
    }
    CONFDIR_BACKUPSDIR = print2string ("%s/backups", CONFDIR);
    CONFDIR_CRASHDIR = print2string ("%s/crash-reports", CONFDIR);

    if (TMPFSDIR == NULL) {
        char *xdgruntimedir = getenv ("XDG_RUNTIME_DIR");

        if (xdgruntimedir == NULL) {
            TMPFSDIR = print2string ("/run/user/%d/bor", pw->pw_uid);
        } else {
            TMPFSDIR = print2string ("%s/bor", xdgruntimedir);
        }
    }

    if (SCRIPTDIR == NULL) {
        SCRIPTDIR = strdup (SHAREDIR "/bor/scripts");
    }

    if (CONFDIR == NULL || CONFDIR_BACKUPSDIR == NULL || TMPFSDIR == NULL
        || SCRIPTDIR == NULL) {
        LOG (LOG_ERROR, "failed initializing directory paths");
        return -1;
    }

    LOG (LOG_DEBUG, "home directory is %s", HOMEDIR);
    LOG (LOG_DEBUG, "conf directory is %s", CONFDIR);
    LOG (LOG_DEBUG, "tmpfs directory is %s", TMPFSDIR);
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
    if (chdir (CONFDIR) == -1) return -1;
    ;

    if (*browsers == NULL) {
        *browsers = calloc (MAX_BROWSERS, sizeof (**browsers));

        if (*browsers == NULL) return -1;
    }
    *browsers_len = 0;

    struct stat sb;

    if (!EXISTS ("browsers.conf")) {
        LOG (LOG_ERROR, "browsers.conf does not exist");
        return -1;
    } else if (!S_ISREG (sb.st_mode) && !S_ISLNK (sb.st_mode)) {
        LOG (LOG_ERROR, "browsers.conf is not a file");
        return -1;
    }

    FILE *conf_fp = fopen ("browsers.conf", "r");
    FILE *exclude_fp = NULL;

    if (conf_fp == NULL) {
        LOG (LOG_ERROR, "failed opening browsers.conf");
        return -1;
    }

    if (EXISTS ("exclude.conf")) {
        exclude_fp = fopen ("exclude.conf", "r");

        if (exclude_fp == NULL) {
            LOG (LOG_ERROR, "failed opening exclude.conf");
            return -1;
        }
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

        char *buf = NULL, *ebuf = NULL;
        size_t buf_size = 0, ebuf_size = 0;
        FILE *pp = popen (cmd, "r");

        if (pp == NULL) return -1;

        struct Browser browser
            = { .name = strdup (browsername),
                .dirs = calloc (MAX_DIRS, sizeof (*(browser.dirs))),
                .dirs_len = 0 };
        if (browser.name == NULL) return -1;
        if (browser.dirs == NULL) return -1;

        LOG (LOG_DEBUG, "got browser %s", browser.name);

        // read from shell script output (directories to sync)
        // format: <dirtype> <path>
        while (getline (&buf, &buf_size, pp) != -1) {
            buf = trim (buf);

            char *delim = strchr (buf, ' ');

            if (delim == NULL) {
                LOG (LOG_WARN, "no directory type provided");
                continue;
            }

            char *typestr = buf, *path = delim + 1;
            enum DirType type = 0;

            *delim = 0;

            for (int i = 1; i < TYPES_LEN; i++) {
                if (strcmp (typestr, DirType_str[i]) == 0) {
                    type = i;
                    break;
                }
            }

            if (type == 0) {
                LOG (LOG_WARN, "Unknown directory type '%'", typestr);
                continue;
            }

            // ignore if directory configured to be excluded
            if (exclude_fp != NULL) {
                int exclude = false;

                while (getline (&ebuf, &ebuf_size, exclude_fp) != -1) {
                    ebuf = trim (ebuf);
                    if (strcmp (path, ebuf) == 0) {
                        exclude = true;
                        break;
                    }
                }
                rewind (exclude_fp);

                if (exclude) continue;
            }

            struct Dir dir = { .path = strdup (path),
                               .dirname = strdup (basename (path)),
                               .type = type };

            if (dir.path == NULL) return -1;
            if (dir.dirname == NULL) return -1;

            LOG (LOG_DEBUG, "received %s dir %s", DirType_str[type], dir.path);

            browser.dirs[browser.dirs_len] = dir;
            browser.dirs_len++;
        }

        if (browser.dirs_len == 0) {
            LOG (LOG_INFO, "no directories configured for %s", browser.name);
        }

        (*browsers)[*browsers_len] = browser;
        (*browsers_len)++;

        free (cmd);
        free (buf);
        free (ebuf);
        pclose (pp);
    }

    free (browsername);
    fclose (conf_fp);

    if (exclude_fp != NULL) fclose (exclude_fp);

    if (*browsers_len == 0) {
        LOG (LOG_INFO, "no browsers configured");
    }

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

    // TODO: add action to clear crash directories
    // check if browsers proccesses are running
    if (!IGNORE_CHECK && action == 's') {
        for (size_t i = 0; i < browsers_len; i++) {
            if (pgrep (browsers[i].name) != -1) {
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
                    PERROR ();
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

    char *uniq_name = filename_wtime (basename (_path), "-crashreport");

    if (uniq_name == NULL) {
        err = -1;
        goto exit;
    }

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
            // only recover if its a profile directory
            if (dir.type == TYPE_PROFILE) {
                if (recover (dir.dirname, browsername) == -1) {
                    LOG (LOG_ERROR, "failed recovering tmpfs");
                    return -1;
                }
            } else {
                LOG (LOG_ERROR, "directory is cache, removing it");
            }
        } else {
            LOG (LOG_INFO, "using tmpfs copy instead of original directory");

            errno = 0;
            if (remove (dir.path) == -1 && errno != ENOENT) return -1;
            ;
            if (move (dir.dirname, dir.path) == -1) return -1;
        }
    }
    // if dir.dirname is a file or cache dir then delete it
    remove_r (dir.dirname);

    // backups are only for profile dirs
    if (dir.type == TYPE_PROFILE) {
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
        remove_r (dir.dirname);
    }

    if (!DIREXISTS (dir.path)) {
        LOG (LOG_ERROR, "directory does not exist");
        return -1;
    }

    /*
       if profile:
       1. copy directory to tmpfs
       2. move directory to backups
       3. symlink directory to tmpfs

       if cache:
       1. move directory to tmpfs
       3. symlink
    */

    if (chdir (TMPFSDIR) == -1) return -1;
    if (chdir (browsername) == -1) return -1;

    if (dir.type == TYPE_PROFILE) {
        if (copy_r (dir.path, dir.dirname) == -1) {
            LOG (LOG_ERROR, "failed copying directory to tmpfs");
            return -1;
        }
    } else if (dir.type == TYPE_CACHE) {
        if (move (dir.path, dir.dirname) == -1) {
            LOG (LOG_ERROR, "failed moving directory to tmpfs");
            return -1;
        }
    }

    char *tmpfs_rlpath = realpath (dir.dirname, NULL);

    if (tmpfs_rlpath == NULL) return -1;

    if (chdir (CONFDIR_BACKUPSDIR) == -1 || chdir (browsername) == -1) {
        free (tmpfs_rlpath);
        return -1;
    }

    if (dir.type == TYPE_PROFILE) {
        if (move (dir.path, dir.dirname) == -1) {
            LOG (LOG_ERROR, "failed moving directory to backups");
            free (tmpfs_rlpath);
            return -1;
        }
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
            LOG (LOG_ERROR, "symlink is not actually a symlink");
        } else {
            LOG (LOG_ERROR, "symlink does not exist");
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

    if (dir.type == TYPE_PROFILE) {
        if (EXISTS (dir.dirname)) {
            if (remove_r (dir.dirname) == -1) {
                LOG (LOG_ERROR, "failed removing backup");
                free (tmpfs_path);
                return -1;
            }
        } else {
            LOG (LOG_WARN, "did not find backup copy of directory");
        }
    }

    free (tmpfs_path);

    return 0;
}

int resync_dir (const struct Dir dir, const char *browsername) {
    if (dir.type == TYPE_CACHE) {
        LOG (LOG_DEBUG, "%s is cache, not resyncing", dir.path);
        return 0;
    }

    errno = 0;
    struct stat sb;

    LOG (LOG_INFO, "resyncing directory %s", dir.path);

    /*
        1. chdir to backups
        2. get realpath of symlink (tmpfs dir)
        3. copy tmpfs over
    */
    if (!SYMEXISTS (dir.path)) {
        if (LEXISTS (dir.path)) {
            LOG (LOG_ERROR, "symlink is not actually a symlink");
        } else {
            LOG (LOG_ERROR, "symlink does not exist");
        }
        return -1;
    }

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
