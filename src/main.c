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
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>
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
static char *RUNTIMEDIR = NULL;
static char *CONFDIR = NULL;
static char *CONFDIR_BACKUPSDIR = NULL;
static char *CONFDIR_CRASHDIR = NULL;
static char *TMPFSDIR = NULL;
static char *SCRIPTDIR = NULL;
static int IGNORE_CHECK = false;
static int SETUID = false;
static uid_t USERID = 0;

enum DirType { TYPE_PROFILE = 1, TYPE_CACHE, TYPES_LEN, TYPES_ERROR };

static const char *DirType_str[] = {NULL, "profile", "cache"};

struct Dir {
    char *dirname;
    char *path;
    enum DirType type;
};

struct Browser {
    char *name;
    char procname[17];
    struct Dir *dirs;
    size_t dirs_len;
};

struct Config {
    int enable_overlay;
    int resync_cache;
} CONFIG = {0};

static const struct option opts[] = {
    {"sync", no_argument, NULL, 's'},
    {"unsync", no_argument, NULL, 'u'},
    {"resync", no_argument, NULL, 'r'},
    {"status", no_argument, NULL, 'p'},
    {"clear", no_argument, NULL, 'x'},
    {"ignore", no_argument, NULL, 'i'},
    {"config", required_argument, NULL, 'c'},
    {"sharedir", required_argument, NULL, 'd'},
    {"runtimedir", required_argument, NULL, 't'},
    {"verbose", no_argument, NULL, 'v'},
    {"version", no_argument, NULL, 'V'},
    {"help", no_argument, NULL, 'h'},
    {0, 0, 0, 0}
};

void help (void);
int status (void);
int read_browsersconf (struct Browser **browsers, size_t *browsers_len);

int init (void);
int init_config (void);

int dir_synced (struct Dir dir);
int do_action (int action);
int recover (const char *path, const char *browsername);

int sync_dir (const struct Dir dir, const char *browsername, int overlay);
int unsync_dir (const struct Dir dir, const char *browsername);
int resync_dir (const struct Dir dir, const char *browsername);

int clear_recovery (void);
int mount_overlay (void);
int unmount_overlay (void);
int overlay_exists (void);
int dir_is_rwx (const char *path);

int main (int argc, char **argv) {

    // too lazy to handle setgid (no use anyways);
    if (getegid () != getgid ()) {
        printf ("program has setgid bit, aborting\n");
        return 1;
    }

    // save user id to switch back after escalation
    USERID = getuid ();

    // drop permissions immediately if setuid bit is set
    // we only escalate when mounting overlay
    if (geteuid () == 0) {
        SETUID = true;

        if (seteuid (USERID) == -1) {
            printf ("seteuid failed\n");
            PERROR ();
            return 1;
        };
    } else if (geteuid () != getuid ()) {
        // abort if setuid is set but it is not root
        printf ("program is not owned by root but has a setuid bit, aborting\n"
        );
        return 1;
    }

    int opt;
    int longindex;
    char action = 0;

    while ((opt = getopt_long (argc, argv, "surpxic:d:t:vVh", opts, &longindex)
           ) != -1) {
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
        case 'V':
            printf ("BROWSER-ON-RAM " VERSION "\n");
            return 0;
        case 'p':
            action = 'p';
            break;
        case 'x':
            action = 'x';
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

            if (dn_rlpath == NULL) {
                LOG (LOG_ERROR, "config directory does not exist");
                return 1;
            }
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
            if (share_rlpath == NULL) {
                LOG (LOG_ERROR, "share directory does not exist");
                return 1;
            }

            SCRIPTDIR = print2string ("%s/scripts", share_rlpath);

            if (SCRIPTDIR == NULL) {
                PERROR ();
                return 1;
            }
            break;
        }
        case 't': {
            RUNTIMEDIR = realpath (optarg, NULL);

            if (RUNTIMEDIR == NULL) {
                PERROR ();
                LOG (LOG_ERROR, "runtime directory does not exist");
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
    if (SETUID == true) {
        LOG (
            LOG_DEBUG,
            "running as setuid program, real userid: %d | effective userid: "
            "%d",
            getuid (), geteuid ()
        );
    }

    if (init () == -1) {
        LOG (LOG_ERROR, "failed initializing");
        PERROR ();
        return 1;
    }

    if (init_config () == -1) {
        LOG (LOG_ERROR, "failed reading config");
        PERROR ();
        return 1;
    }

    if (action == 's' || action == 'r' || action == 'u') {
        // check if rsync is not available
        if (system ("which rsync > /dev/null 2>&1")) {
            LOG (LOG_ERROR, "could not find rsync, please install it");
            return 1;
        }

        if (do_action (action) == -1) {
            LOG (LOG_ERROR, "failed attempting to sync/unsync/resync");
            return 1;
        }

        return 0;
    }

    // status
    if (action == 'p') {
        if (status () == -1) {
            LOG (LOG_ERROR, "status failed");
            PERROR ();
            return 1;
        };
    }

    // delete recovery directories
    if (action == 'x') {
        if (clear_recovery () == -1) {
            LOG (LOG_ERROR, "failed clearing recovery directories");
            PERROR ();
            return 1;
        }
    }

    return 0;
}
// clang-format off
void help(void) {
    printf ("BROWSER-ON-RAM "VERSION"\n\n");
    printf ("Usage: bor [OPTION]\n");
    printf ("-s, --sync             sync browsers to memory\n");
    printf ("-u, --unsync           unsync browsers\n");
    printf ("-r, --resync           resync browsers\n");
    printf ("-p, --status           show current status and configuration\n");
    printf ("-x, --clear            clear recovery directories\n");
    printf ("-i, --ignore           ignore safety & lock checks\n");
    printf ("-c, --config           override config directory location\n");
    printf ("-d, --sharedir         override data/share directory location\n");
    printf ("-t, --runtimedir       override runtime directory location\n");
    printf ("-v, --verbose          enable debug logs\n");
    printf ("-V, --version          show program version\n");
    printf ("-h, --help             show this message\n\n");
    printf ("It is not recommended to use sync, unsync, or resync standalone.\n");
    printf ("Please use the systemd user service instead\n");
}
// clang-format on

int status (void) {
    struct stat sb;

    struct Browser *browsers = NULL;
    size_t browsers_len = 0;

    if (read_browsersconf (&browsers, &browsers_len) == -1) return -1;

    printf ("Browser-on-RAM " VERSION "\n\n");

    char *srv_active, *timer_active;

    srv_active = systemd_userservice_active ("bor.service") ? "true" : "false";
    timer_active =
        systemd_userservice_active ("bor-resync.timer") ? "true" : "false";

    printf ("%-20s%s\n", "Systemd service:", srv_active);
    printf ("%-20s%s\n", "Systemd timer:", timer_active);

    int overlay_active = overlay_exists ();

    printf ("%-20s%s\n", "Overlay status:", overlay_active ? "true" : "false");

    // show overlay size (physical data in tmpfs)
    if (overlay_active) {
        if (chdir (RUNTIMEDIR) == -1) return -1;

        off_t size = get_dir_size (".bor-upper");

        if (size != -1) {
            char *human = human_readable (size);
            printf ("%-20s%s\n", "Overlay size:", human);
            free (human);
        }
    }

    printf ("\nConfigured directories\n\n");

    char *buf = calloc (PATH_MAX + 1, sizeof (*buf));
    char *bcrashdir = calloc (PATH_MAX + 1, sizeof (*bcrashdir));

    if (buf == NULL || bcrashdir == NULL) return -1;

    for (size_t b = 0; b < browsers_len; b++) {
        struct Browser browser = browsers[b];

        printf ("%c%s:\n", toupper (browser.name[0]), browser.name + 1);

        snprintf (
            bcrashdir, PATH_MAX + 1, "%s/%s", CONFDIR_CRASHDIR, browser.name
        );

        DIR *dp = NULL;
        struct dirent *de = NULL;

        if (stat (bcrashdir, &sb) == 0) {
            dp = opendir (bcrashdir);
            if (dp == NULL) return -1;
        }

        for (size_t d = 0; d < browser.dirs_len; d++) {
            struct Dir dir = browser.dirs[d];
            off_t dir_size = get_dir_size (dir.path);

            printf ("\n");
            printf ("%-20s%s\n", "Type:", DirType_str[dir.type]);

            if (dir_size != -1) {
                printf ("%-20s%s\n", "Directory:", dir.path);
            } else {
                printf (
                    "%-20s%s%s\n", "Directory:", dir.path, "(DOES NOT EXIST!)"
                );
            }

            // get tmpfs path
            snprintf (
                buf, PATH_MAX + 1, "%s/%s/%s", TMPFSDIR, browser.name,
                dir.dirname
            );

            if (stat (buf, &sb) == 0) {
                printf ("%-20s%s\n", "Tmpfs:", buf);
            }

            if (dir_size != -1) {
                char *human = human_readable (dir_size);
                printf ("%-20s%s\n", "Directory size:", human);
                free (human);
            }

            // get any crash recovery directories if crashdir
            // for browser exists
            if (dp != NULL) {
                while ((de = readdir (dp)) != NULL) {
                    if (de->d_type == DT_DIR) {
                        char *str_start = strstr (de->d_name, "-crashreport");

                        // remove -crashreport* substring
                        if (str_start == NULL) continue;
                        char prevc = *str_start;
                        *str_start = 0;

                        if (strcmp (de->d_name, dir.dirname) != 0) continue;

                        *str_start = prevc;
                        printf (
                            "%-20s%s/%s\n", "Crash directory:", buf, de->d_name
                        );
                    }
                }
                rewinddir (dp);
            }
        }
        if (dp != NULL) closedir (dp);
    }
    free (buf);
    free (bcrashdir);
    return 0;
}

// initialize required dirs and create browsers.conf template
int init (void) {
    struct passwd *pw = getpwuid (USERID);

    HOMEDIR = strdup (pw->pw_dir);

    if (HOMEDIR == NULL) return -1;

    // only set confdir if it wasnt given by user
    if (CONFDIR == NULL) {
        // follow XDG base spec
        char *xdgconfighome = secure_getenv ("XDG_CONFIG_HOME");

        if (xdgconfighome == NULL) {
            CONFDIR = print2string ("%s/.config/bor", HOMEDIR);
        } else {
            CONFDIR = print2string ("%s/bor", xdgconfighome);
        }
    }
    if (CONFDIR == NULL) return -1;

    CONFDIR_BACKUPSDIR = print2string ("%s/backups", CONFDIR);
    CONFDIR_CRASHDIR = print2string ("%s/crash-reports", CONFDIR);

    char *xdgruntimedir = secure_getenv ("XDG_RUNTIME_DIR");

    if (RUNTIMEDIR == NULL) {
        if (xdgruntimedir == NULL) {
            RUNTIMEDIR = print2string ("/run/user/%d", USERID);
            TMPFSDIR = print2string ("%s/bor", RUNTIMEDIR, USERID);
        } else {
            RUNTIMEDIR = strdup (xdgruntimedir);
            TMPFSDIR = print2string ("%s/bor", xdgruntimedir);
        }
    } else {
        TMPFSDIR = print2string ("%s/bor", RUNTIMEDIR, USERID);
    }

    if (RUNTIMEDIR == NULL) return -1;

    // check if user has set sharedir (therefor scriptdir), else use the macro
    // value
    if (SCRIPTDIR == NULL) {
        SCRIPTDIR = strdup (SHAREDIR "/bor/scripts");
    }

    if (CONFDIR_BACKUPSDIR == NULL || TMPFSDIR == NULL || SCRIPTDIR == NULL) {
        LOG (LOG_ERROR, "failed initializing directory paths");
        return -1;
    }

    LOG (LOG_DEBUG, "home directory is %s", HOMEDIR);
    LOG (LOG_DEBUG, "runtime directory is %s", RUNTIMEDIR);
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
        FILE *bcfp = fopen ("browsers.conf", "w");

        if (bcfp == NULL) {
            LOG (LOG_ERROR, "failed creating browser.conf");
            return -1;
        }

        fprintf (
            bcfp, "# each line corrosponds to a browser that should be "
                  "synced, ex:\n"
        );
        fprintf (bcfp, "# firefox\n# chromium\n");

        fclose (bcfp);
    }

    return 0;
}

// read & initialize config file & config structure
int init_config (void) {
    struct stat sb;

    // defaults
    CONFIG.enable_overlay = false;
    CONFIG.resync_cache = true;

    // NOLINTNEXTLINE
    if (chdir (CONFDIR) == -1) return -1;

    // don't read if config file doesnt exist
    if (!EXISTS ("bor.conf")) return 0;

    FILE *conf_fp = fopen ("bor.conf", "r");

    if (conf_fp == NULL) {
        LOG (LOG_ERROR, "failed opening bor.conf");
        return -1;
    }

    char *buf = NULL;
    size_t buf_size;

    // config file format: <key>=<value>
    while (getline (&buf, &buf_size, conf_fp) != -1) {
        buf = trim (buf);

        char *equal_sign = strchr (buf, '=');

        if (equal_sign == NULL) {
            LOG (LOG_WARN, "invalid config option %s", buf);
            continue;
        }

        char *key = buf, *value = equal_sign + 1;

        *equal_sign = 0; // split key and value
        key = trim (key);
        value = trim (value);

        // read config
        if (strcmp (key, "enable_overlay") == 0) {
            int boolean = get_bool (value);

            if (boolean == -1) goto invalid;
            CONFIG.enable_overlay = boolean;
        } else if (strcmp (key, "resync_cache") == 0) {
            int boolean = get_bool (value);

            if (boolean == -1) goto invalid;
            CONFIG.resync_cache = boolean;
        } else {
            LOG (LOG_WARN, "unknown config option %s = %s", key, value);
        }
        LOG (LOG_DEBUG, "received config option %s = %s", key, value);
        continue;
    invalid:
        LOG (LOG_WARN, "invalid value for config option %s = %s", value, key);
    }

    free (buf);
    fclose (conf_fp);

    return 0;
}

// read browsers.conf and return array of structs for dirs to synchronize
int read_browsersconf (struct Browser **browsers, size_t *browsers_len) {
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

        if (chdir (SCRIPTDIR) == -1) {
            return -1;
        }

        char *filename = print2string ("%s.sh", browsername);

        if (!EXISTS (filename)) {
            LOG (
                LOG_WARN, "script for %s does not exist, excluding browser",
                browsername
            );
            free (filename);
            continue;
        }

        free (filename);
        char *cmd = print2string ("exec sh ./%s.sh", browsername);
        if (cmd == NULL) {
            return -1;
        }

        char *buf = NULL, *ebuf = NULL;
        size_t buf_size = 0, ebuf_size = 0;
        FILE *pp = popen (cmd, "r");

        free (cmd);
        if (pp == NULL) {
            return -1;
        }

        struct Browser browser = {
            .name = strdup (browsername),
            .procname = {0},
            .dirs = calloc (MAX_DIRS, sizeof (*(browser.dirs))),
            .dirs_len = 0,
        };

        if (browser.name == NULL || browser.dirs == NULL) {
            return -1;
        }

        LOG (LOG_DEBUG, "got browser %s", browser.name);

        // only set proccess name once for each browser
        int got_procname = false;

        // read from shell script output (directories to sync)
        // format: <procname> <dirtype> <path>
        while (getline (&buf, &buf_size, pp) != -1) {
            buf = trim (buf);

            char procname[17] = {0}, typestr[50] = {0};
            char *path = calloc (PATH_MAX + 1, sizeof (*path));

            if (path == NULL) return -1;

            enum DirType type = TYPES_ERROR;

            int nread = 0;
            int sscanf_ret =
                sscanf (buf, "%s %s %n", procname, typestr, &nread);

            if (sscanf_ret != 2) {
                LOG (
                    LOG_WARN, "failed parsing shell script output \"%s\"", buf
                );
                LOG (LOG_INFO, "%d", sscanf_ret);
                PERROR ();
                continue;
            }

            // rest of buf is path
            strncpy (path, buf + nread, PATH_MAX + 1);

            if (!got_procname) {
                got_procname = true;
                LOG (LOG_DEBUG, "browser proccess name is \"%s\"", procname);

                strncpy (browser.procname, procname, 17);
            }

            // compare dir type to database of dir types
            for (int i = 1; i < TYPES_LEN; i++) {
                if (strcmp (typestr, DirType_str[i]) == 0) {
                    type = i;
                    break;
                }
            }
            if (type == TYPES_ERROR) {
                LOG (LOG_WARN, "Unknown directory type '%'", typestr);
                continue;
            }

            // ignore if directory configured to be excluded
            if (exclude_fp != NULL) {
                int exclude = false;

                // read through exclude.conf
                while (getline (&ebuf, &ebuf_size, exclude_fp) != -1) {
                    ebuf = trim (ebuf);
                    if (strcmp (path, ebuf) == 0) {
                        exclude = true;
                        break;
                    }
                }
                if (fseek (exclude_fp, 0L, SEEK_SET) == -1) return -1;
                clearerr (exclude_fp);

                if (exclude) continue;
            }

            struct Dir dir = {
                .path = path, // already allocated memory
                .dirname = strdup (basename (path)),
                .type = type
            };

            if (dir.path == NULL || dir.dirname == NULL) {
                return -1;
            }

            LOG (LOG_DEBUG, "received %s dir %s", DirType_str[type], dir.path);

            browser.dirs[browser.dirs_len] = dir;
            browser.dirs_len++;
        }

        if (browser.dirs_len == 0) {
            LOG (LOG_INFO, "no directories configured for %s", browser.name);
        }

        (*browsers)[*browsers_len] = browser;
        (*browsers_len)++;

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

// check if directory is synced
int dir_synced (struct Dir dir) {
    // directory is synced if it is a symlink that points to a directory
    struct stat sb;

    // don't bother with errors, just assume false
    if (lstat (dir.path, &sb) == -1) return false;

    if (!S_ISLNK (sb.st_mode)) {
        return false;
    }

    if (stat (dir.path, &sb) == -1 || !S_ISDIR (sb.st_mode)) {
        return false;
    }

    return true;
}

int do_action (int action) {
    struct Browser *browsers = NULL;
    size_t browsers_len = 0;

    if (read_browsersconf (&browsers, &browsers_len) == -1) {
        LOG (LOG_ERROR, "failed reading browser.conf");
        return -1;
    }

    // check if browsers proccesses are running
    if (!IGNORE_CHECK && action == 's') {
        for (size_t i = 0; i < browsers_len; i++) {
            LOG (LOG_INFO, "%s", browsers[i].procname);
            if (pgrep (browsers[i].procname) != -1) {
                LOG (LOG_ERROR, "%s is running, aborting", browsers[i].name);
                return -1;
            }
        }
    }

    int is_overlay = false;

    if (CONFIG.enable_overlay && action == 's') {
        if (SETUID) {
            is_overlay = true;
        } else {
            LOG (
                LOG_WARN,
                "unable to mount overlay filesystem because setuid bit "
                "is not configured"
            );
        }
    }

    for (size_t b = 0; b < browsers_len; b++) {
        struct Browser browser = browsers[b];

        if (action == 's') {
            // create directories to store dirs for browser
            if (chdir (CONFDIR_BACKUPSDIR) == -1) continue;
            if (mkdir_p (browser.name, 0755) == -1) continue;

            if (chdir (TMPFSDIR) == -1) continue;
            if (mkdir_p (browser.name, 0755) == -1) continue;

            LOG (LOG_INFO, "syncing %s", browser.name);
        } else if (action == 'u') {
            LOG (LOG_INFO, "unsyncing %s", browser.name);
        } else if (action == 'r') {
            LOG (LOG_INFO, "resyncing %s", browser.name);
        }
        int count = 0;

        for (size_t d = 0; d < browser.dirs_len; d++) {
            struct Dir dir = browser.dirs[d];

            if (action == 's' && dir_synced (dir)) {
                LOG (LOG_DEBUG, "%s is already synced", dir.path);
                continue;
            }
            if ((action == 'u' || action == 'r') && !dir_synced (dir)) {
                LOG (LOG_DEBUG, "%s is not synced", dir.path);
                continue;
            }

            if (action == 's') {
                // we set lock after fully syncing everything (in case of
                // overlay fs)
                if (sync_dir (dir, browser.name, is_overlay) == -1) {
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
            count++;
        }
        if (count == 0) {
            LOG (
                LOG_INFO, "%s: no action done for any directory ", browser.name
            );
        }

        // delete browser directory if it is empty
        if (action == 'u') {
            if (chdir (CONFDIR_BACKUPSDIR) == 0) {
                rmdir (browser.name);
            }
            if (chdir (TMPFSDIR) == 0 && !is_overlay) {
                rmdir (browser.name);
            }
        }
    }

    // mount overlay filesystem
    if (is_overlay && action == 's') {
        if (mount_overlay () == -1) {
            LOG (LOG_ERROR, "could not mount overlay");
            PERROR ();
            return -1;
        }
    }

    // unmount overlay (always check)
    if (action == 'u') {
        if (unmount_overlay () == -1) {
            LOG (LOG_ERROR, "could not unmount overlay");
            PERROR ();
            return -1;
        }
    }

    // delete tmpfs directory
    if (action == 'u') {
        if (chdir (RUNTIMEDIR) == 0) {
            rmdir ("bor");
        }
    }

    return 0;
}

int recover (const char *path, const char *browsername) {
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

    if (chdir (CONFDIR_CRASHDIR) == -1 || mkdir_p (browsername, 0755) == -1 ||
        chdir (browsername) == -1) {
        err = -1;
        goto exit;
    }

    char *uniq_name = filename_wtime (basename (_path), "-crashreport");

    if (uniq_name == NULL) {
        err = -1;
        goto exit;
    }

    // remove existing directory if it exists

    remove_r (uniq_name);
    if (move (rlpath, uniq_name) == -1) {
        LOG (LOG_ERROR, "could not move directory to crash dir");
        free (uniq_name);
        err = -1;
        goto exit;
    }

exit:
    if (prevcwd != NULL) chdir (prevcwd);
    free (_path);
    free (rlpath);
    free (prevcwd);
    return err;
}

int sync_dir (const struct Dir dir, const char *browsername, int overlay) {
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
        if (LEXISTS (dir.path) && !backup_exists) {
            // only recover if its a profile directory
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

    if (chdir (CONFDIR_BACKUPSDIR) == -1) return -1;
    if (chdir (browsername) == -1) return -1;

    if (LEXISTS (dir.dirname)) {
        LOG (LOG_WARN, "found backup copy of directory");
        if (EXISTS (dir.path)) {
            if (recover (dir.dirname, browsername) == -1) {
                LOG (LOG_ERROR, "failed recovering backup");
                return -1;
            }
        } else {
            LOG (
                LOG_INFO, "directory doesn't exist, using backup copy instead"
            );

            if (remove (dir.path) == -1) return -1;
            ;
            if (move (dir.dirname, dir.path) == -1) return -1;
        }
    }

    if (!DIREXISTS (dir.path)) {
        LOG (LOG_ERROR, "directory does not exist");
        return -1;
    }

    /*
       1. copy directory to tmpfs
       2. move directory to backups
       3. symlink directory to tmpfs

       if its an overlay then dont copy to tmpfs
    */
    char *tmpfs_rlpath = NULL;

    if (chdir (TMPFSDIR) == -1) return -1;
    if (chdir (browsername) == -1) return -1;

    if (overlay) {
        // can't get tmpfs path via realpath(),
        // so combine realpath of tmpfs dir + dirname

        char *tmp = realpath (".", NULL);

        tmpfs_rlpath = print2string ("%s/%s", tmp, dir.dirname);
    } else {
        if (copy_r (dir.path, dir.dirname) == -1) {
            LOG (LOG_ERROR, "failed copying directory to tmpfs");
            return -1;
        }
        tmpfs_rlpath = realpath (dir.dirname, NULL);
    }

    if (tmpfs_rlpath == NULL) return -1;

    if (chdir (CONFDIR_BACKUPSDIR) == -1 || chdir (browsername) == -1) {
        free (tmpfs_rlpath);
        return -1;
    }

    if (move (dir.path, dir.dirname) == -1) {
        LOG (LOG_ERROR, "failed moving directory to backups");

        remove_r (tmpfs_rlpath);

        free (tmpfs_rlpath);
        return -1;
    }

    if (symlink (tmpfs_rlpath, dir.path) == -1) {
        LOG (LOG_ERROR, "failed creating symlink");

        remove_r (tmpfs_rlpath);
        move (dir.dirname, dir.path); // move backup back

        free (tmpfs_rlpath);
        return -1;
    }

    free (tmpfs_rlpath);
    return 0;
}

int unsync_dir (const struct Dir dir, const char *browsername) {
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
    if (dir.type == TYPE_CACHE && !CONFIG.resync_cache) {
        LOG (LOG_DEBUG, "%s is cache, not resyncing", dir.path);
        return 0;
    }

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

int clear_recovery (void) {
    struct Browser *browsers = NULL;
    size_t browsers_len = 0;

    if (read_browsersconf (&browsers, &browsers_len) == -1) {
        free (browsers);
        return -1;
    }

    LOG (LOG_INFO, "clearing recovery directories");

    char *buf = calloc (PATH_MAX + 1, sizeof (*buf));

    if (buf == NULL) {
        free (browsers);
        return -1;
    }

    for (size_t b = 0; b < browsers_len; b++) {
        struct Browser browser = browsers[b];

        for (size_t d = 0; d < browser.dirs_len; d++) {
            struct Dir dir = browser.dirs[d];

            // read crash directory folder for browser
            snprintf (
                buf, PATH_MAX + 1, "%s/%s", CONFDIR_CRASHDIR, browser.name
            );
            DIR *dp = opendir (buf);

            if (dp == NULL || chdir (buf) == -1) {
                free (browsers);
                free (buf);
                return -1;
            }

            struct dirent *de = NULL;

            while ((de = readdir (dp)) != NULL) {
                if (de->d_type == DT_DIR) {
                    // cut off -crashreport part and compare to original
                    // directory
                    char *str_start = strstr (de->d_name, "-crashreport");

                    // remove -crashreport* substring
                    if (str_start == NULL) continue;
                    char prevc = *str_start;
                    *str_start = 0;

                    // skip if dirname without -crashreport doesn't match
                    // original directory (we don't want to accidently non
                    // crash directories)
                    if (strcmp (de->d_name, dir.dirname) != 0) continue;

                    *str_start = prevc;

                    if (remove_r (de->d_name) == -1) {
                        LOG (
                            LOG_WARN, "failed removing %s/%s", buf, de->d_name
                        );
                        continue;
                    } else {
                        LOG (LOG_INFO, "removing %s/%s", buf, de->d_name);
                    }
                }
            }

            closedir (dp);
        }
    }
    free (browsers);
    free (buf);
    return 0;
}

int mount_overlay (void) {
    if (!SETUID) {
        LOG (
            LOG_ERROR, "cannot mount overlay filesystem, program does not "
                       "have a setuid bit"
        );
        return -1;
    }

    int o_exists = overlay_exists ();

    if (o_exists == true) {
        LOG (LOG_ERROR, "overlay filesystem is already mounted");
        return -1;
    } else if (o_exists == -1) {
        LOG (LOG_ERROR, "failed checking if overlay filesystem exists");
        return -1;
    }

    LOG (LOG_INFO, "mounting overlay filesystem");

    /*
       How overlay works (should happen before directories are synced):
       We set the entire backups directory as the lower directory
       and a separate tmpfs directory as the upper dir and another one for the
       workdir. The normal tmpfs dir is used as the "merged" directory browser.
       Then profiles/caches are symlinked as normal
    */

    if (chdir (RUNTIMEDIR) == -1) return -1;

    if (mkdir_p (".bor-upper", 0755) == -1) return -1;
    if (mkdir_p (".bor-work", 0755) == -1) return -1;

    const char *data = print2string (
        "lowerdir=%s,upperdir=.bor-upper,workdir=.bor-work", CONFDIR_BACKUPSDIR
    );
    unsigned long flags = MS_NOATIME | MS_NODEV | MS_NOSUID;

    if (seteuid (0) == -1) return -1;

    // check if directories have full perms
    // overlay ignores permissions of lowerdir
    if (!dir_is_rwx (CONFDIR_BACKUPSDIR) || !dir_is_rwx (".bor-upper") ||
        !dir_is_rwx ("bor") || !dir_is_rwx (".bor-work")) {
        return -1;
    }

    if (mount ("overlay", TMPFSDIR, "overlay", flags, data) == -1) {
        LOG (LOG_ERROR, "failed mounting overlay filesystem");

        remove_r (".bor-work");
        if (seteuid (USERID) == -1) return -1;
        remove_r (".bor-upper");

        return -1;
    }
    if (seteuid (USERID) == -1) return -1;

    return 0;
}

int unmount_overlay (void) {
    struct stat sb;

    // check if overlay filesystem actually exists first
    int o_exists = overlay_exists ();

    if (o_exists == false) {
        return 0;
    } else if (o_exists == -1) {
        LOG (LOG_ERROR, "failed checking if overlay filesystem exists");
        return -1;
    }

    if (!SETUID) {
        LOG (
            LOG_ERROR, "cannot unmount overlay filesystem, program does not "
                       "have a setuid bit"
        );
        return -1;
    }

    LOG (LOG_INFO, "umounting overlay filesystem");

    if (seteuid (0) == -1) return -1;

    // lazy unmount
    if (umount2 (TMPFSDIR, UMOUNT_NOFOLLOW | MNT_DETACH) == -1) {
        if (seteuid (USERID) == -1) return -1;
        LOG (LOG_ERROR, "failed unmounting overlay filesystem");
        return -1;
    }
    if (seteuid (USERID) == -1) return -1;

    if (chdir (RUNTIMEDIR) == -1) return -1;

    if (remove_r (".bor-upper") == -1) return -1;

    // do not remove if workdir is a symlink
    if (lstat (".bor-work", &sb) == -1) return -1;
    if (S_ISLNK (sb.st_mode)) {
        LOG (LOG_ERROR, "work directory for overlay filsystem is a symlink");
        return -1;
    }

    // work dir needs perms to remove
    if (seteuid (0) == -1) return -1;
    if (remove_r (".bor-work") == -1) {
        if (seteuid (USERID) == -1) return -1;
        ;
        return -1;
    }
    if (seteuid (USERID) == -1) return -1;

    return 0;
}

// return true/false if overlay exists/nonexistent on tmpfs, -1 on error
int overlay_exists (void) {
    struct stat sb, psb;

    if (stat (TMPFSDIR, &sb) == -1) return -1;
    if (stat (RUNTIMEDIR, &psb) == -1) return -1;

    // tmpfs is not a mountpoint if device ids match
    if (sb.st_dev == psb.st_dev) {
        return false;
    }

    // check if filesystem type is overlay
    struct statfs fsb;

    if (statfs (TMPFSDIR, &fsb) == -1) return -1;
    // OVERLAYFS_SUPER_MAGIC
    if (fsb.f_type != 0x794c7630) {
        LOG (
            LOG_ERROR, "tmpfs dir is on a different filesystem than the "
                       "runtime directory"
        );
        return -1;
    }

    return true;
}

// check if directory has full permissions by real uid of process
int dir_is_rwx (const char *path) {
    char *prevcwd = get_current_dir_name ();

    if (prevcwd == NULL) return false;

    // save euid to restore later
    int err = 0;
    uid_t euid = geteuid ();

    if (seteuid (USERID) == -1) {
        chdir (prevcwd);
        return false;
    }

    // check if directory is rw via diropen() and opening a file in it
    DIR *dirfp = opendir (path);

    // assume errors as false
    if (dirfp == NULL) err = true;
    if (chdir (path) == -1) err = true;

    int fd = creat (".test", 0644);

    if (fd == -1) {
        err = true;
    } else {
        close (fd);
        unlink (".test");
    }

    if (dirfp != NULL) closedir (dirfp);

    if (seteuid (euid) == -1) {
        chdir (prevcwd);
        return false;
    }

    if (err) {
        LOG (LOG_WARN, "process does not have full permissions for %s", path);
        chdir (prevcwd);
        return false;
    }
    chdir (prevcwd);

    return true;
}
