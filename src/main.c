#include "config.h"
#include "sync.h"
#include "util.h"

#include <getopt.h>
#include <unistd.h>

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/capability.h>

#ifndef VERSION
#define VERSION "UNKNOWN"
#endif

int do_action(enum Action action);

int init(bool save_config);
int uninit(void);

void print_help(void);
void print_status(void);

int main(int argc, char **argv)
{
        struct option long_options[] = { { "version", no_argument, NULL, 'V' },
                                         { "verbose", no_argument, NULL, 'v' },
                                         { "help", no_argument, NULL, 'h' },
                                         { "sync", no_argument, NULL, 's' },
                                         { "unsync", no_argument, NULL, 'u' },
                                         { "resync", no_argument, NULL, 'r' },
                                         { "clean", no_argument, NULL, 'c' },
                                         { "status", no_argument, NULL, 'p' },
                                         { NULL, 0, NULL, 0 } };

        int opt, opt_index;
        enum Action action = ACTION_NONE;
        bool status = false;

        while ((opt = getopt_long(argc, argv, "Vvhsurcp", long_options,
                                  &opt_index)) != -1) {
                switch (opt) {
                case 'v':
                        LOG_LEVEL = LOG_DEBUG;
                        break;
                case 'V':
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
                        return 0;
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
        }

        plog(LOG_INFO, "starting browser-on-ram " VERSION);

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
                return -1;
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
                if (do_action_on_browser(browser, action) == -1) {
                        plog(LOG_WARN, "failed %sing browser %s",
                             action_str[action], browser->name);
                        continue;
                }
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

void print_help(void)
{
        printf("Browser-on-ram " VERSION "\n");
        printf("Usage: bor [option]\n\n");

        printf("Options:\n");
        printf("--version               show version\n");
        printf("--verbose               show debug logs\n");
        printf("--help                  show this message\n");
        printf("--sync                  do sync\n");
        printf("--unsync                do unsync\n");
        printf("--resync                do resync\n");
        printf("--clean                 remove recovery directories\n");
        printf("--status                show current configuration & state\n");

        printf("\nNot recommended to use sync functions directly.\n");
        printf("Please use the systemd service and timer\n");
}

void print_status(void)
{
        init(false);

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

        printf("\nDirectories:\n\n");

        struct stat sb;
        char backup[PATH_MAX];
        char tmpfs[PATH_MAX];

        for (size_t i = 0; i < CONFIG.browsers_num; i++) {
                struct Browser *browser = CONFIG.browsers[i];

                printf("Browser: %s\n", browser->name);

                for (size_t k = 0; k < browser->dirs_num; k++) {
                        struct Dir *dir = browser->dirs[k];

                        if (get_paths(dir, backup, tmpfs) == -1) {
                                printf("Error");
                                continue;
                        }
                        bool dir_exists = false;
                        char *size = human_readable(get_dir_size(dir->path));
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
                                printf("Size:              %s\n", size);
                        }
                        printf("\n");

                        free(size);
                }
        }
}

// vim: sw=8 ts=8
