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
                             { "verbose", no_argument, NULL, 'v' },
                             { "help", no_argument, NULL, 'h' },
                             { 0, 0, 0, 0 } };
    int opt;
    int longindex;
    char action = 0;

    while ((opt = getopt_long (argc, argv, "survphn", opts, &longindex))
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

    switch (action) {
    case 's': {

        struct Browser *browsers = NULL;
        size_t browsers_len = 0;

        if (read_browsersconf (&browsers, &browsers_len) == -1) {
            LOG (LOG_ERROR, "failed reading browser.conf");
            err = 1;
            break;
        }
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
    printf ("-v, --verbose          enable debug logs\n");
    printf ("-h, --help             show this message\n\n");
    printf ("It is not recommended to use sync, unsync, or resync standalone.\n");
    printf ("Please use the systemd user service instead\n");
}
// clang-format on

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
    if (stat (SCRIPTDIR, &sb) == -1) {
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

    if (stat ("browsers.conf", &sb) == -1) {
        int fd = creat ("browsers.conf", 0644);

        dprintf (fd, "# each line corrosponds to a browser that should be "
                     "synced, ex:\n");
        dprintf (fd, "# firefox\n# chromium\n");

        close (fd);
    }

    return 0;
}

struct Dir create_dir_s (char **buffer, const char *browsername) {
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
    if (*browsers == NULL) {
        *browsers = calloc (MAX_BROWSERS, sizeof (**browsers));

        CHECKALLOC (*browsers, true);
    }
    *browsers_len = 0;

    chdir (CONFDIR);

    struct stat sb;

    if (stat ("browsers.conf", &sb) == -1) {
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
        char *cmd = print2string ("./%s.sh", browsername);
        char *buf = NULL;
        size_t dirpath_size = 0;
        FILE *pp = popen (cmd, "r");

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
            (*browsers)[*browsers_len] = browser;
            (*browsers_len)++;
            continue;
        }

        if (ent->fts_info != FTS_D) continue;
        // ignore erverything else

        if (ent->fts_level != 1 && ent->fts_level != 2) continue;

        if (ent->fts_level == 1 && ent->fts_info == FTS_D) {
            // browser dir
            browser.name = strdup (ent->fts_name);
            CHECKALLOC (browser.name, true);

            browser.dirs = calloc (MAX_DIRS, sizeof (*(browser.dirs)));
            CHECKALLOC (browser.dirs, true);

        } else if (ent->fts_level == 2) {
            // dir to sync
            ent->fts_path = replace_char (ent->fts_name, '\\', '/');

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
    struct stat sb;

    LOG (LOG_INFO, "recovering %s", path);

    // check if path exists
    if (stat (path, &sb) == -1) {
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

    if (dir == NULL) return -1;

    do {
        LOG (LOG_INFO, "syncing %s", dir->path);
        // check if path exists
        if (lstat (dir->path, &sb) == -1) {
            LOG (LOG_WARN, "sync target %s does not exist, skipping",
                 dir->path);
            continue;
        }
        // check if path is not a dir
        if (!S_ISDIR (sb.st_mode)) {
            // check if its a valid symlink pointing to a directory
            if (S_ISLNK (sb.st_mode) && stat (dir->path, &sb) == 0) {
                LOG (LOG_ERROR,
                     "%s is a symlink pointing to a directory, aborting",
                     dir->path);
                return -1;
            } else if (S_ISLNK (sb.st_mode)) {
                // path is a dangling symlink
                // attempt to use backups as sync target if it exists
                if (stat (dir->backup_path, &sb) == 0
                    || S_ISDIR (sb.st_mode)) {

                    LOG (LOG_WARN, "%s is a dangling symlink, using backups",
                         dir->path);
                } else {
                    LOG (LOG_WARN, "%s is a dangling symlink, skipping",
                         dir->path);
                }

                // delete symlink and move backups to path
                if (remove (dir->path) == -1) {
                    LOG (LOG_WARN, "failed deleting symlink, skipping");
                    continue;
                }
                if (rename (dir->backup_path, dir->path) == -1) {
                    LOG (LOG_WARN, "failed moving backup back, skipping");
                    continue;
                }

            } else {
                LOG (LOG_WARN, "sync target %s is not a directory, skipping",
                     dir->path);
                continue;
            }
        }

        // check if dir exists in tmpfs
        if (lstat (dir->tmp_path, &sb) == 0 && S_ISDIR (sb.st_mode)) {
            LOG (LOG_WARN, "found %s in tmpfs dir, recovering", dir->dirname);
            if (recover_dir (dir->tmp_path, browsername) == -1) {
                LOG (LOG_WARN, "failed recovering, skipping");
            }
        }
        // check if dir exists in backups
        if (lstat (dir->backup_path, &sb) == 0 && !S_ISDIR (sb.st_mode)) {
            LOG (LOG_WARN, "found %s in backup dir, recovering", dir->dirname);
            if (recover_dir (dir->backup_path, browsername) == -1) {
                LOG (LOG_WARN, "failed recovering, skipping");
            }
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
             "could not find any synced directories to resync, aborting");
        return -1;
    }

    do {
        LOG (LOG_DEBUG, "resyncing %s", dir->path);

        // check if paths exist
        if (stat (dir->path, &sb) == -1 || stat (dir->tmp_path, &sb) == -1) {
            LOG (LOG_WARN, "Cannot resync %s, required dirs don't exist",
                 dir->backup_path);
            continue;
        }

        char *tmp_bkuppath = create_unique_filename (dir->backup_path, "-tmp");

        CHECKALLOC (tmp_bkuppath, true);

        // copy tmpfs dir over to backups
        if (copy_r (dir->tmp_path, tmp_bkuppath) == -1) {
            LOG (LOG_WARN, "failed copying %s to %s, skipping ", dir->tmp_path,
                 tmp_bkuppath);
            PERROR ();
            continue;
        }

        // swap backup and new backup
        if (renameat2 (AT_FDCWD, dir->backup_path, AT_FDCWD, tmp_bkuppath,
                       RENAME_EXCHANGE)
            == -1) {
            LOG (LOG_WARN, "failed swapping %s and %s, skipping ",
                 dir->backup_path, tmp_bkuppath);
            PERROR ();
            remove_r (tmp_bkuppath);
            continue;
        }
        remove_r (tmp_bkuppath);

        free (tmp_bkuppath);
        LOG (LOG_INFO, "successfully resynced %s", dir->path);

    } while ((dir = walk_browsers (NULL, 0, &browsername)) != NULL);

    return 0;
}

int do_unsync (void) {
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

        // check if directories exist
        if (lstat (dir->path, &sb) == -1) {
            LOG (LOG_WARN, "symlink for %s doesn't exist", dir->path);
        } else if (S_ISLNK (sb.st_mode) && stat (dir->path, &sb) == -1) {
            LOG (LOG_WARN, "symlink for %s is dangling", dir->path);
        }
        if (lstat (dir->path, &sb) == 0 && S_ISDIR (sb.st_mode)) {
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
