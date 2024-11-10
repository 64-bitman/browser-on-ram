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
    char *orig_dirname;
    char *backup_path;
    char *tmp_path;
    char *path;
};

struct Browser {
    char *name;
    struct Dir *dirs;
    size_t dirs_len;
};

void help (void);
void status (void);
int toggle_lock (void);

int initialize_dirs (void);
struct Dir create_dir_s (char **buffer, const char *browsername);
int read_browsersconf (struct Browser **browsers, size_t *browsers_len);

int get_browsers (char *rootpath, struct Browser **browsers,
                  size_t *browsers_len);
struct Dir *walk_browsers (struct Browser *browsers, size_t browsers_len,
                           char **browsername);
int do_sync (struct Browser *browsers, size_t browsers_len);
int do_resync (void);
int do_unsync (void);

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
    // TODO: add option to ignore specific directories
    // TODO: look in /usr/share and /usr/local and xdg data dir for scripts

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
        err = do_resync () * -1;
        break;
    case 'u': {
        if (do_resync () == -1) {
            err = 1;
            break;
        }
        err = do_unsync () * -1;
        if (toggle_lock () == -1) return -1;
        break;
    }
    case 'p':
        status ();
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

void status (void) {
    errno = 0;
    if (chdir (CONFDIR) == -1) return;

    struct stat sb;
    int lock_exists = (EXISTS ("lock")) ? true : false;

    printf (BOLD "Browser-on-RAM " VERSION "\n\n" RESET);

    {
        printf (BOLD "---Status---\n\n" RESET);
        printf (BOLD BLUE);
        printf ("Active:            %s\n", (lock_exists) ? "true" : "false");
        printf ("Systemd service:   %s\n",
                (systemd_userservice_active ("bor.service")) ? "active"
                                                             : "inactive");
        printf ("Resync timer:      %s\n",
                (systemd_userservice_active ("bor-resync.timer"))
                    ? "active"
                    : "inactive");
        printf (RESET);
    }
    printf (BOLD "\n---Configured Directories---\n" RESET);
    fflush (stdout);

    struct Browser *browsers = NULL;
    size_t browsers_len = 0;

    // use backups dir to find directories if lock exists
    // else use browser conf
    if (lock_exists) {
        if (get_browsers (CONFDIR_BACKUPSDIR, &browsers, &browsers_len) == -1)
            return;
    } else {
        if (read_browsersconf (&browsers, &browsers_len) == -1) return;
    }

    char *browsername = NULL;
    struct Dir *dir = walk_browsers (browsers, browsers_len, &browsername);

    if (dir == NULL) return;

    do {
        int path_exists = EXISTS (dir->path) ? true : false;
        int tmp_exists = EXISTS (dir->tmp_path) ? true : false;
        int backup_exists = EXISTS (dir->backup_path) ? true : false;

        printf ("\n");
        printf ("Browser:           " YELLOW "%s\n" RESET, browsername);
        printf ("Directory:         " UNDERLINE "%s" RESET " %s\n", dir->path,
                (path_exists) ? "" : "(does not exist)");
        if (lock_exists) {
            printf ("Tmpfs directory:   %s/" BOLD RED "%s" RESET " %s\n",
                    TMPDIR, dir->dirname,
                    (tmp_exists) ? "" : "(does not exist)");
            printf ("Backup directory:  %s/" BOLD RED "%s" RESET " %s\n",
                    CONFDIR_BACKUPSDIR, dir->dirname,
                    (backup_exists) ? "" : "(does not exist)");
        }
        printf ("Directory size:    " GREEN "%s\n" RESET,
                human_readable (get_dir_size (dir->path)));

    } while ((dir = walk_browsers (NULL, 0, &browsername)) != NULL);

    // print crash recovery dirs
    printf (BOLD "\n---Crash Recovery---\n" RESET);
    fflush (stdout);

    browsers = NULL;
    browsers_len = 0;
    if (get_browsers (CONFDIR_CRASHDIR, &browsers, &browsers_len) == -1)
        return;

    dir = walk_browsers (browsers, browsers_len, &browsername);

    if (dir == NULL) return;

    do {
        printf ("\nBrowser:         %s", browsername);
    } while ((dir = walk_browsers (NULL, 0, &browsername)));
}

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
        PERROR ();
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
    if (mkdir (CONFDIR, 0755) == -1 && errno != EEXIST) return -1;
    if (mkdir (CONFDIR_BACKUPSDIR, 0755) == -1 && errno != EEXIST) return -1;
    if (mkdir (CONFDIR_CRASHDIR, 0755) == -1 && errno != EEXIST) return -1;
    if (mkdir (TMPDIR, 0755) == -1 && errno != EEXIST) return -1;

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

struct Dir create_dir_s (char **buffer, const char *browsername) {
    errno = 0;
    char *buf = *buffer;

    struct Dir dir = { 0 };

    if ((dir.path = strdup (buf)) == NULL) return (struct Dir){ 0 };

    char *tmp = strdup (buf);

    if (tmp == NULL) return (struct Dir){ 0 };
    if ((dir.orig_dirname = strdup (basename (tmp))) == NULL)
        return (struct Dir){ 0 };

    // replace forwardslash with backslash
    buf = replace_char (buf, '/', '\\');

    if ((dir.dirname = strdup (buf)) == NULL) return (struct Dir){ 0 };

    dir.tmp_path = print2string ("%s/%s/%s", TMPDIR, browsername, buf);
    dir.backup_path
        = print2string ("%s/%s/%s", CONFDIR_BACKUPSDIR, browsername, buf);

    return dir;
}

// read browsers.conf and return array of structs for dirs to synchronize
int read_browsersconf (struct Browser **browsers, size_t *browsers_len) {
    errno = 0;
    if (*browsers == NULL) {
        *browsers = calloc (MAX_BROWSERS, sizeof (**browsers));

        CHECKALLOC (*browsers, true);
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
        PERROR ();
        return -1;
    }

    // read file
    char *browsername = NULL;
    size_t browsername_size = 0;

    while (getline (&browsername, &browsername_size, conf_fp) != -1) {
        browsername = trim (browsername);

        // ignore comments (#)
        if (browsername[0] == '#') continue;

        LOG (LOG_DEBUG, "got browser %s", browsername);

        struct Browser browser = { 0 };

        CHECKALLOC ((browser.name = strdup (browsername)), true);

        if (chdir (SCRIPTDIR) == -1) return -1;

        // read from shell script output
        char *cmd = print2string ("sh ./%s.sh", browsername);

        CHECKALLOC (cmd, true);
        char *buf = NULL;
        size_t dirpath_size = 0;
        FILE *pp = popen (cmd, "r");

        if (pp == NULL) {
            PERROR ();
            return -1;
        }
        struct Dir *dirs = calloc (MAX_DIRS, sizeof (*dirs));

        CHECKALLOC (dirs, true);
        browser.dirs = dirs;

        while (getline (&buf, &dirpath_size, pp) != -1) {
            buf = trim (buf);
            LOG (LOG_DEBUG, "received dir %s", buf);

            struct Dir dir = create_dir_s (&buf, browsername);

            if (dir.path == NULL) return -1;

            dirs[browser.dirs_len] = dir;
            (browser.dirs_len)++;
        }
        if (browser.dirs_len == 0) {
            LOG (LOG_DEBUG, "received no directories from %s", browsername);
        }

        free (cmd);
        free (buf);
        pclose (pp);

        // add to browser array
        (*browsers)[*browsers_len] = browser;
        (*browsers_len)++;
    }

    free (browsername);
    fclose (conf_fp);

    return 0;
}

// go through root and find dirs from each browser dir
int get_browsers (char *rootpath, struct Browser **browsers,
                  size_t *browsers_len) {
    errno = 0;
    if (*browsers == NULL) {
        *browsers = calloc (MAX_BROWSERS, sizeof (**browsers));

        CHECKALLOC (*browsers, true);
    }

    char *paths[] = { rootpath, NULL };

    FTS *ftsp = fts_open (paths, FTS_PHYSICAL | FTS_XDEV, NULL);
    struct Browser browser = { 0 };
    FTSENT *ent = NULL;

    while ((ent = fts_read (ftsp)) != NULL) {
        if (ent->fts_info == FTS_DP && ent->fts_level == 1) {
            // add browser struct to array after init finished
            if (browser.dirs_len == 0) {
                LOG (LOG_DEBUG, "received no directories from %s",
                     browser.name);
            }

            (*browsers)[*browsers_len] = browser;
            (*browsers_len)++;
            continue;
        }

        if (ent->fts_info != FTS_D) continue;
        // ignore everything else

        if (ent->fts_level != 1 && ent->fts_level != 2) continue;

        if (ent->fts_level == 1 && ent->fts_info == FTS_D) {
            // browser dir
            LOG (LOG_DEBUG, "got browser %s", ent->fts_name);
            browser.name = strdup (ent->fts_name);
            CHECKALLOC (browser.name, true);

            browser.dirs = calloc (MAX_DIRS, sizeof (*(browser.dirs)));
            CHECKALLOC (browser.dirs, true);

        } else if (ent->fts_level == 2) {
            // dir to sync
            ent->fts_path = replace_char (ent->fts_name, '\\', '/');
            LOG (LOG_DEBUG, "received dir %s", ent->fts_path);

            struct Dir dir = create_dir_s (&(ent->fts_path), browser.name);

            CHECKALLOC (dir.path, true);

            browser.dirs[browser.dirs_len] = dir;
            browser.dirs_len++;
        }
    }

    fts_close (ftsp);

    return 0;
}

struct Dir *walk_browsers (struct Browser *browsers, size_t browsers_len,
                           char **browsername) {
    static size_t b_i, d_i, b_len;
    static struct Browser *b_struct = NULL;

    if (browsers != NULL) {
        b_len = browsers_len;
        b_struct = browsers;
        b_i = d_i = 0;
        *browsername = b_struct[b_i].name;
    }

    if (b_struct != NULL) {
        struct Dir *dir = &(b_struct[b_i].dirs[d_i]);

        if (d_i++ == b_struct[b_i].dirs_len) {
            if (++b_i == b_len) {
                // finished
                b_struct = NULL;
                b_i = d_i = b_len = 0;
                return NULL;
            }
            d_i = 0;
            *browsername = b_struct[b_i].name;
        }
        return dir;
    } else
        return NULL;
}

int recover_dir (const char *path, const char *browsername) {
    errno = 0;
    struct stat sb;

    LOG (LOG_INFO, "recovering %s", path);

    // check if path does not exist
    if (!EXISTS (path)) {
        LOG (LOG_ERROR, "cannot recover non-existent path %s", path);
        return -1;
    }
    LOG (LOG_INFO, "recovering %s", path);

    // create browser dir
    if (chdir (CONFDIR_CRASHDIR) == -1) return -1;
    if (mkdir (browsername, 0755) == -1) return -1;
    if (chdir (browsername) == -1) return -1;

    // copy path to crash directory
    char *_path = strdup (path);
    CHECKALLOC (_path, true);
    char *dirname = create_unique_filename (basename (_path), "-crashreport");
    free (_path);

    if (copy_r (path, dirname) == -1) {
        LOG (LOG_ERROR, "failed copying %s to crash dir", path);
        PERROR ();
        free (dirname);
        return -1;
    }

    // remove path
    if (remove_r (path) == -1) {
        LOG (LOG_ERROR, "failed removing %s ", path);
        PERROR ();
        free (dirname);
        return -1;
    }

    free (dirname);

    return 0;
}

int do_sync (struct Browser *browsers, size_t browsers_len) {
    errno = 0;
    // create browser dirs
    LOG (LOG_INFO, "starting sync");
    for (size_t b = 0; b < browsers_len; b++) {
        if (chdir (CONFDIR_BACKUPSDIR) == -1) return -1;
        if (mkdir (browsers[b].name, 0755) == -1 && errno != EEXIST) return -1;
        if (chdir (TMPDIR) == -1) return -1;
        if (mkdir (browsers[b].name, 0755) == -1 && errno != EEXIST) return -1;
        errno = 0;
    }

    char *browsername = NULL;
    struct Dir *dir = walk_browsers (browsers, browsers_len, &browsername);
    struct stat sb;
    char *abrt_browser = "";

    if (dir == NULL) return -1;

    do {
        // skip if browser set to be skipped previously
        if (strcmp (abrt_browser, browsername) == 0) {
            continue;
        }
        // skip browser if process running
        if (pgrep (browsername) != -1 && !IGNORE_CHECK) {
            LOG (LOG_ERROR, "%s instance detected, skipping browser",
                 browsername);
            abrt_browser = browsername;
            continue;
        }
        LOG (LOG_INFO, "syncing %s", dir->path);

        // check if path does not exist
        if (!LEXISTS (dir->path)) {

            // check if tmpfs or backup dir exists
            // if so, then attempt to move/copy tmpfs/backup to path
            // and use it as sync target
            if (EXISTS (dir->tmp_path) && S_ISDIR (sb.st_mode)) {
                if (copy_r (dir->tmp_path, dir->path) == -1) continue;
                if (remove_r (dir->tmp_path) == -1) continue;

                LOG (LOG_WARN, "path non-existent, but tmpfs path exists, "
                               "using tmpfs instead");
            } else if (EXISTS (dir->backup_path) && S_ISDIR (sb.st_mode)) {
                if (rename (dir->backup_path, dir->path) == -1) continue;

                LOG (LOG_WARN, "path non-existent, but backup path exists, "
                               "using backup instead");
            } else {
                LOG (LOG_WARN, "path does not exist, skipping", dir->path);
                continue;
            }
        }

        // check if path is not a dir
        if (!S_ISDIR (sb.st_mode)) {

            // skip browser if it is a valid symlink
            if (S_ISLNK (sb.st_mode) && EXISTS (dir->path)) {
                LOG (LOG_ERROR, "%s is a valid symlink, skipping %s",
                     dir->path, browsername);
                abrt_browser = "firefox";

            } else if (S_ISLNK (sb.st_mode)) {
                // path is a dangling symlink
                // attempt to use backups as sync target if it exists
                if (EXISTS (dir->backup_path) && S_ISDIR (sb.st_mode)) {

                    LOG (LOG_WARN, "%s is a dangling symlink, using backups",
                         dir->path);
                } else {
                    LOG (LOG_WARN, "%s is a dangling symlink, skipping",
                         dir->path);
                    continue;
                }

                // delete symlink and move backups to path
                if (remove (dir->path) == -1) continue;
                if (rename (dir->backup_path, dir->path) == -1) continue;

            } else {
                LOG (LOG_WARN, "sync target %s is not a directory, skipping",
                     dir->path);
                continue;
            }
        }

        // check if dir exists in tmpfs, if so recover
        if (EXISTS (dir->tmp_path) && S_ISDIR (sb.st_mode)) {
            LOG (LOG_WARN, "found %s in tmpfs dir, recovering", dir->dirname);

            if (recover_dir (dir->tmp_path, browsername) == -1) continue;
        }
        // check if dir exists in backups, if so recover
        if (EXISTS (dir->backup_path) && S_ISDIR (sb.st_mode)) {
            LOG (LOG_WARN, "found %s in backup dir, recovering", dir->dirname);

            if (recover_dir (dir->backup_path, browsername) == -1) continue;
        }

        remove (dir->tmp_path);
        remove (dir->backup_path);

        // copy dir to tmpfs
        if (copy_r (dir->path, dir->tmp_path) == -1) {
            LOG (LOG_WARN, "failed copying %s to %s, skipping ", dir->path,
                 dir->tmp_path);
            remove_r (dir->tmp_path);
            continue;
        }

        // move dir to backups
        if (rename (dir->path, dir->backup_path) == -1) {
            LOG (LOG_WARN, "failed moving %s to backups, skipping", dir->path);

            remove_r (dir->tmp_path);
            continue;
        }

        // symlink
        if (symlink (dir->tmp_path, dir->path) == -1) {
            LOG (LOG_WARN, "failed creating symlink for %s, skipping",
                 dir->path);
            remove_r (dir->tmp_path);
            rename (dir->backup_path, dir->path);
        }

        LOG (LOG_INFO, "successfully synced %s", dir->path);
    } while ((dir = walk_browsers (NULL, 0, &browsername)) != NULL);

    return 0;
}

int do_resync (void) {
    errno = 0;
    struct Browser *browsers = NULL;
    size_t browsers_len = 0;

    if (get_browsers (CONFDIR_BACKUPSDIR, &browsers, &browsers_len) == -1) {
        return -1;
    };

    char *browsername = NULL;
    struct Dir *dir = walk_browsers (browsers, browsers_len, &browsername);
    struct stat sb;

    if (dir == NULL) {
        LOG (LOG_WARN,
             "could not find any synced directories to resync, aborting");
        return -1;
    }

    do {
        LOG (LOG_DEBUG, "resyncing %s", dir->path);

        // check if path and tmpfs path does not exist
        if (!EXISTS (dir->path) || !EXISTS (dir->tmp_path)) {
            LOG (LOG_WARN, "Cannot resync %s, required dirs don't exist",
                 dir->backup_path);
            continue;
        }

        // copy tmpfs dir over to backups
        if (copy_r (dir->tmp_path, dir->backup_path) == -1) {
            LOG (LOG_WARN, "failed copying %s to %s, skipping ", dir->tmp_path,
                 dir->backup_path);
            PERROR ();
            continue;
        }

        LOG (LOG_INFO, "successfully resynced %s", dir->path);

    } while ((dir = walk_browsers (NULL, 0, &browsername)) != NULL);

    return 0;
}

int do_unsync (void) {
    errno = 0;
    struct Browser *browsers = NULL;
    size_t browsers_len = 0;

    if (get_browsers (CONFDIR_BACKUPSDIR, &browsers, &browsers_len) == -1) {
        PERROR ();
        return -1;
    };

    char *browsername = NULL;
    struct Dir *dir = walk_browsers (browsers, browsers_len, &browsername);
    struct stat sb;

    if (dir == NULL) {
        LOG (LOG_WARN,
             "could not find any synced directories to unsync, aborting");
        return -1;
    }

    do {
        LOG (LOG_DEBUG, "unsyncing %s", dir->path);

        // check if symlinks do not exist or are dangling
        if (!LEXISTS (dir->path)) {
            LOG (LOG_WARN, "symlink for %s doesn't exist", dir->path);

        } else if (S_ISLNK (sb.st_mode) && !EXISTS (dir->path)) {
            LOG (LOG_WARN, "symlink for %s is dangling", dir->path);

        } else if (S_ISDIR (sb.st_mode)) {
            // if path is not a symlink but a dir, recover
            LOG (LOG_WARN, "%s is a directory, recovering", dir->path);
            recover_dir (dir->path, browsername);
        }
        remove (dir->path);

        // swap backup to path
        if (rename (dir->backup_path, dir->path) == -1) {
            LOG (LOG_WARN, "failed moving %s to %s", dir->backup_path,
                 dir->path);
            PERROR ();
            continue;
        }

        errno = 0;
        if (remove_r (dir->tmp_path) == -1 && errno != ENOENT) {
            LOG (LOG_WARN, "failed removing path %s", dir->tmp_path);
        }

        LOG (LOG_INFO, "successfully unsynced %s", dir->path);

    } while ((dir = walk_browsers (NULL, 0, &browsername)) != NULL);

    return 0;
}
