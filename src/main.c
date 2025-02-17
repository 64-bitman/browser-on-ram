#include "config.h"
#include "sync.h"
#include "overlay.h"
#include "util.h"

#include <dirent.h>
#include <getopt.h>
#include <unistd.h>
#include <glob.h>

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef VERSION
#define VERSION "UNKNOWN"
#endif

int do_action(enum Action action);

int init(bool save_config);
int uninit(void);

int mount_overlay(void);
int unmount_overlay(void);
bool overlay_mounted(void);

int clear_recovery_dirs(void);
int remove_glob(glob_t *gb);
int get_recovery_dirs(struct Dir *target_dir, glob_t *glob_struct);

void print_help(void);
void print_status(void);

// TODO: implement clean option

int main(int argc, char **argv)
{
        struct option long_options[] = { { "version", no_argument, NULL, 'v' },
                                         { "verbose", no_argument, NULL, 'V' },
                                         { "help", no_argument, NULL, 'h' },
                                         { "sync", no_argument, NULL, 's' },
                                         { "unsync", no_argument, NULL, 'u' },
                                         { "resync", no_argument, NULL, 'r' },
                                         { "clean", no_argument, NULL, 'c' },
                                         { "status", no_argument, NULL, 'p' },
                                         { NULL, 0, NULL, 0 } };

        int opt, opt_index;
        enum Action action = ACTION_NONE;
        bool status = false, clean = false;

        while ((opt = getopt_long(argc, argv, "Vvhsurcp", long_options,
                                  &opt_index)) != -1) {
                switch (opt) {
                case 'V':
                        LOG_LEVEL = LOG_DEBUG;
                        break;
                case 'v':
                        printf("browser-on-ram version " VERSION "\n");
                        return 0;
                case 'h':
                        print_help();
                        return 0;
                case 's':
                        action = ACTION_SYNC;
                        break;
                case 'u':
                        action = ACTION_UNSYNC;
                        break;
                case 'r':
                        action = ACTION_RESYNC;
                        break;
                case 'c':
                        clean = true;
                        break;
                case 'p':
                        status = true;
                        break;
                default:
                        return 0;
                }
        }
        if (status) {
                print_status();
                return 0;
        } else if (clean) {
                return (clear_recovery_dirs() == -1) ? 1 : 0;
        }

        plog(LOG_INFO, "starting browser-on-ram " VERSION);

        // need rsync
        if (!program_exists("rsync")) {
                plog(LOG_ERROR, "rsync is required, please install");
                return 1;
        }

        if (do_action(action) == -1) {
                plog(LOG_ERROR, "failed sync/unsync/resync");
                return 1;
        }

        return 0;
}

// loop through configured browsers and do sync/unsync/resync on them
int do_action(enum Action action)
{
        if (init(true) == -1) {
                plog(LOG_ERROR, "failed initializing");
                return -1;
        }
        if (create_dir(PATHS.backups, 0755) == -1 ||
            create_dir(PATHS.tmpfs, 0755) == -1) {
                plog(LOG_ERROR, "failed creating required directories");
                PERROR();
                return -1;
        }
        size_t did_action = 0;
        bool overlay = false;

        // check if we have required capabilities
        // do it before any action so that unsync/resync
        // works properly in case we don't
        if (CONFIG.enable_overlay &&
            !check_caps_state(CAP_PERMITTED, CAP_SET, 2, CAP_SYS_ADMIN,
                              CAP_DAC_OVERRIDE)) {
                plog(LOG_WARN, "CAP_SYS_ADMIN and CAP_DAC_OVERRIDE "
                               "is needed for overlay feature");
        } else if (CONFIG.enable_overlay) {
                overlay = true;
        }

        for (size_t i = 0; i < CONFIG.browsers_num; i++) {
                struct Browser *browser = CONFIG.browsers[i];

                // don't sync if browser is running
                if (get_pid(browser->procname) != -1 && action == ACTION_SYNC) {
                        plog(LOG_WARN, "%s is running, skipping",
                             browser->name);
                        continue;
                }
                // if a directory or entire browser was not u/r/synced (error)
                // then skip it and still continue
                if (do_action_on_browser(browser, action, overlay) == -1) {
                        plog(LOG_WARN, "failed %sing browser %s",
                             action_str[action], browser->name);
                        continue;
                }
                did_action++;
        }
        // we mount after because modifying lowerdir before mount
        // doesn't reflect changes
        if (did_action > 0 && overlay && action == ACTION_SYNC) {
                if (overlay_mounted()) {
                        plog(LOG_WARN,
                             "tmpfs is mounted, cannot mount overlay; please check");
                } else if (mount_overlay() == -1) {
                        plog(LOG_ERROR, "failed creating overlay");
                        return -1;
                }
        }

        if (!overlay && overlay_mounted()) {
                plog(LOG_WARN,
                     "tmpfs is mounted, but required capabilities do not exist");
        } else if (action == ACTION_UNSYNC && overlay_mounted() &&
                   unmount_overlay() == -1) {
                plog(LOG_ERROR, "failed removing overlay");
                return -1;
        }

        if (action == ACTION_UNSYNC && uninit() == -1) {
                plog(LOG_WARN, "failed uninitializing");
        }

        return 0;
}

// initialize paths and config
// if save_config is true then make a .bor.conf file to save state
int init(bool save_config)
{
        plog(LOG_DEBUG, "initializing");
        if (init_paths() == -1) {
                plog(LOG_ERROR, "failed initializing paths");
                return -1;
        }
        if (init_config(save_config) == -1) {
                plog(LOG_ERROR, "failed initializing config");
                return -1;
        }

        return 0;
}

// remove certain directories and files (should be done after unsync)
int uninit(void)
{
        struct stat sb;

        // cleanup directories
        if (DIREXISTS(PATHS.tmpfs) && rmdir(PATHS.tmpfs) == -1) {
                plog(LOG_WARN, "failed removing %s", PATHS.tmpfs);
                PERROR();
        }

        // delete temporary config file
        char config_file[PATH_MAX];

        snprintf(config_file, PATH_MAX, "%s/.bor.conf", PATHS.config);

        if (FEXISTS(config_file) && unlink(config_file) == -1) {
                plog(LOG_WARN, "failed removing .bor.conf");
                PERROR();
        }

        return 0;
}

// initializes paths and config itself
int clear_recovery_dirs(void)
{
        if (init(false) == -1) {
                return -1;
        }

        for (size_t i = 0; i < CONFIG.browsers_num; i++) {
                struct Browser *browser = CONFIG.browsers[i];

                plog(LOG_DEBUG, "clearing browser %s", browser->name);

                for (size_t k = 0; k < browser->dirs_num; k++) {
                        struct Dir *dir = browser->dirs[k];

                        glob_t gb;

                        if (get_recovery_dirs(dir, &gb) == -1) {
                                plog(LOG_WARN, "failed getting directories");
                                continue;
                        }
                        remove_glob(&gb);

                        globfree(&gb);
                }
        }
        return 0;
}

// remove directories specified in glob struct
int remove_glob(glob_t *gb)
{
        for (size_t j = 0; j < gb->gl_pathc; j++) {
                plog(LOG_INFO, "removing %s", gb->gl_pathv[j]);

                if (remove_path(gb->gl_pathv[j]) == -1) {
                        plog(LOG_ERROR, "failed removing recovery directory %s",
                             gb->gl_pathv[j]);
                        PERROR();
                        continue;
                }
        }
        return 0;
}

// return a list of recovery dirs that belong to target_dir in a glob
int get_recovery_dirs(struct Dir *target_dir, glob_t *glob_struct)
{
        plog(LOG_DEBUG, "getting recovery dirs for directory %s",
             target_dir->path);

        // use a glob to get recovery dirs
        char pattern[PATH_MAX];

        snprintf(pattern, PATH_MAX, "%s/" BOR_CRASH_PREFIX "*",
                 target_dir->parent_path);

        int err = glob(pattern, GLOB_NOSORT | GLOB_ONLYDIR, NULL, glob_struct);

        if (err != 0 && err != GLOB_NOMATCH) {
                plog(LOG_ERROR, "failed globbing directories");
                PERROR();
                return -1;
        }

        return 0;
}

void print_help(void)
{
        printf("Browser-on-ram " VERSION "\n");
        printf("Usage: bor [option]\n\n");

        printf("Options:\n");
        printf(" -v, --version               show version\n");
        printf(" -V, --verbose               show debug logs\n");
        printf(" -h, --help                  show this message\n");
        printf(" -s, --sync                  do sync\n");
        printf(" -u, --unsync                do unsync\n");
        printf(" -r, --resync                do resync\n");
        printf(" -c, --clean                 remove recovery directories\n");
        printf(" -p, --status                show current configuration & state\n");

        printf("\nNot recommended to use sync functions directly.\n");
        printf("Please use the systemd service and timer\n");
}

void print_status(void)
{
        if (init(false) == -1) {
                return;
        }

        printf("Browser-on-ram " VERSION "\n");

        bool service_active = sd_uunit_active("bor.service"),
             timer_active = sd_uunit_active("bor-resync.timer");

        printf("\nStatus:\n");
        printf("Systemd service:         %s\n",
               service_active ? "Active" : "Inactive");
        printf("Systemd resync timer:    %s\n",
               timer_active ? "Active" : "Inactive");
        printf("Overlay:                 %s\n",
               CONFIG.enable_overlay ? "Enabled" : "Disabled");

        if (overlay_mounted()) {
                // totol overlay upper size
                char *otosize =
                        human_readable(get_dir_size(PATHS.overlay_upper));

                printf("Total overlay size:      %s\n", otosize);

                free(otosize);
        }
        char *tosize = human_readable(get_dir_size(PATHS.tmpfs));

        printf("Total size               %s\n", tosize);

        free(tosize);

        printf("\nDirectories:\n\n");

        struct stat sb;
        char backup[PATH_MAX], tmpfs[PATH_MAX], otmpfs[PATH_MAX];

        for (size_t i = 0; i < CONFIG.browsers_num; i++) {
                struct Browser *browser = CONFIG.browsers[i];

                printf("Browser: %s\n", browser->name);

                for (size_t k = 0; k < browser->dirs_num; k++) {
                        struct Dir *dir = browser->dirs[k];

                        if (get_paths(dir, backup, tmpfs) == -1) {
                                printf("Error\n");
                                continue;
                        }
                        bool dir_exists = false;
                        char *type = (dir->type == DIR_PROFILE) ? "profile" :
                                     (dir->type == DIR_CACHE)   ? "cache" :
                                                                  "unknown";

                        printf("Type:              %s\n", type);
                        if (DIREXISTS(dir->path) || SYMEXISTS(dir->path)) {
                                printf("Directory:         %s\n", dir->path);
                                dir_exists = true;
                        } else {
                                printf("Directory:         %s (DOES NOT EXIST)\n",
                                       dir->path);
                        }
                        if (DIREXISTS(backup)) {
                                printf("Backup:            %s\n", backup);
                        }
                        if (DIREXISTS(tmpfs)) {
                                printf("Tmpfs:             %s\n", tmpfs);
                        }
                        if (dir_exists) {
                                char *size =
                                        human_readable(get_dir_size(dir->path));
                                printf("Size:              %s\n", size);
                                free(size);
                        }
                        if (overlay_mounted()) {
                                if (get_overlay_paths(dir, otmpfs) == -1) {
                                        printf("Error\n");
                                        continue;
                                }
                                char *osize =
                                        human_readable(get_dir_size(otmpfs));

                                printf("Overlay size:      %s\n", osize);

                                free(osize);
                        }
                        // print recovery dirs
                        glob_t gb;

                        if (get_recovery_dirs(dir, &gb) == 0) {
                                for (size_t j = 0; j < gb.gl_pathc; j++) {
                                        printf("Recovery:          %s\n",
                                               gb.gl_pathv[j]);
                                }

                                globfree(&gb);
                        }

                        printf("\n");
                }
        }
}

// vim: sw=8 ts=8
