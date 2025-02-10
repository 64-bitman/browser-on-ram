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

int init(void);
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
                                         { "status", no_argument, NULL, 'p' },
                                         { NULL, 0, NULL, 0 } };

        int opt, opt_index;
        enum Action action = ACTION_NONE;

        while ((opt = getopt_long(argc, argv, "Vvhsurp", long_options,
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
                case 'p':
                        print_status();
                        break;
                default:
                        return 0;
                }
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
        if (init() == -1) {
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
int init(void)
{
        plog(LOG_DEBUG, "initializing");
        if (init_paths() == -1) {
                plog(LOG_ERROR, "failed initializing paths");
                return -1;
        }
        if (init_config() == -1) {
                plog(LOG_ERROR, "failed initializing config");
                return -1;
        }

        return 0;
}

// remove certain directories and files (should be done after unsync)
int uninit(void)
{
        // cleanup directories
        if (rmdir(PATHS.tmpfs) == -1) {
                plog(LOG_WARN, "failed removing %s", PATHS.tmpfs);
        }

        // delete temporary config file
        char config_file[PATH_MAX];

        snprintf(config_file, PATH_MAX, "%s/.bor.conf", PATHS.config);

        if (unlink(config_file) == -1) {
                plog(LOG_WARN, "failed removing .bor.conf");
        }

        return 0;
}

void print_help(void)
{
}

void print_status(void)
{
}

// vim: sw=8 ts=8
