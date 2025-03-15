#define _GNU_SOURCE
#include "config.h"
#include "log.h"
#include "util.h"
#include "ini.h"

#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <unistd.h>

// OPT_END -> signify end of opt array
enum OptType { OPT_END, OPT_BOOL, OPT_INT };
struct Opt {
        char *name;
        void *data;
        enum OptType type;
};

static int set_environment(void);
static int parse_config(const char *config_file);
static int parse_config_handler(void *user, const char *section,
                                const char *name, const char *value);
static int section_config_handler(const char *name, const char *value);
static int section_browsers_handler(const char *name);
static struct Browser *run_browser_sh(const char *browsername);
static int parse_browser_sh(const char *path, struct Browser *browser);
static int parse_browser_sh_handler(void *user, const char *UNUSED(section),
                                    const char *name, const char *value);

struct ConfigSkel CONFIG = { 0 };
struct PathsSkel PATHS = { 0 };

static struct Opt OPTS[] = {
#ifndef NOOVERLAY
        { "enable_overlay", &CONFIG.enable_overlay, OPT_BOOL },
#endif
        { "enable_cache", &CONFIG.enable_cache, OPT_BOOL },
        { "resync_cache", &CONFIG.resync_cache, OPT_BOOL },
        { "reset_overlay", &CONFIG.reset_overlay, OPT_BOOL },
        { "max_log_entries", &CONFIG.max_log_entries, OPT_INT },
        { NULL, NULL, OPT_END }
};

// initialize paths (does not create them)
int init_paths(void)
{
        plog(LOG_DEBUG, "initializing paths");

        if (set_environment() == -1) {
                return -1;
        }

        snprintf(PATHS.runtime, PATH_MAX, "%s/bor", getenv("XDG_RUNTIME_DIR"));
        snprintf(PATHS.tmpfs, PATH_MAX, "%s/tmpfs", PATHS.runtime);
        snprintf(PATHS.config, PATH_MAX, "%s/bor", getenv("XDG_CONFIG_HOME"));
        snprintf(PATHS.backups, PATH_MAX, "%s/backups", PATHS.config);
        snprintf(PATHS.logs, PATH_MAX, "%s/logs", PATHS.config);
        snprintf(PATHS.share_dir, PATH_MAX, "/usr/share/bor/");
        snprintf(PATHS.share_dir_local, PATH_MAX, "/usr/local/share/bor");

#ifndef NOOVERLAY
        snprintf(PATHS.overlay_upper, PATH_MAX, "%s/upper", PATHS.runtime);
        snprintf(PATHS.overlay_work, PATH_MAX, "%s/work", PATHS.runtime);
#endif

        plog(LOG_DEBUG, "config dir: %s", PATHS.config);
        plog(LOG_DEBUG, "runtime dir: %s", PATHS.runtime);

        return 0;
}

// set the required environment variables
static int set_environment(void)
{
        char xdg_config[PATH_MAX], xdg_cache[PATH_MAX], xdg_run[PATH_MAX],
                xdg_data[PATH_MAX];

        snprintf(xdg_config, PATH_MAX, "%s/.config", getenv("HOME"));
        snprintf(xdg_cache, PATH_MAX, "%s/.cache", getenv("HOME"));
        snprintf(xdg_run, PATH_MAX, "/run/user/%d", getuid());
        snprintf(xdg_data, PATH_MAX, "%s/.local/share", getenv("HOME"));

        update_string(xdg_config, PATH_MAX, getenv("XDG_CONFIG_HOME"));
        update_string(xdg_cache, PATH_MAX, getenv("XDG_CACHE_HOME"));
        update_string(xdg_run, PATH_MAX, getenv("XDG_RUNTIME_DIR"));
        update_string(xdg_data, PATH_MAX, getenv("XDG_DATA_HoME"));

        trim(xdg_config);
        trim(xdg_cache);
        trim(xdg_run);
        trim(xdg_data);

        if (setenv("XDG_CONFIG_HOME", xdg_config, true) == -1 ||
            setenv("XDG_CACHE_HOME", xdg_cache, true) == -1 ||
            setenv("XDG_DATA_HOME", xdg_data, true) == -1 ||
            setenv("XDG_RUNTIME_DIR", xdg_run, true) == -1) {
                plog(LOG_ERROR, "failed setting environment");
                PERROR();
                return -1;
        }

        return 0;
}

// initialize and parse config, requires paths to be initialized before
int init_config(bool save_config)
{
        struct stat sb;

        plog(LOG_DEBUG, "initializing config");

        // defaults
#ifndef NOOVERLAY
        CONFIG.enable_overlay = false;
#endif
        CONFIG.enable_cache = false;
        CONFIG.resync_cache = true;
        CONFIG.reset_overlay = false;
        CONFIG.max_log_entries = 10;

        char borconf[PATH_MAX], dotborconf[PATH_MAX];

        snprintf(borconf, PATH_MAX, "%s/bor.conf", PATHS.config);
        snprintf(dotborconf, PATH_MAX, "%s/.bor.conf", PATHS.config);

        if (!save_config) {
                if (parse_config(borconf) == -1) {
                        return -1;
                }
                return 0;
        }

        // use .bor.conf if it exists, else copy bor.conf as .bor.conf
        // this is so changes don't affect current sync session
        if (FEXISTS(borconf)) {
                if (!FEXISTS(dotborconf)) {
                        if (copy_path(borconf, dotborconf, false) == -1) {
                                plog(LOG_ERROR, "failed copying config file");
                                PERROR();
                                return -1;
                        }
                        if (chmod(dotborconf, 0444) == -1) {
                                PERROR();
                        }
                }
        } else {
                plog(LOG_ERROR, "config file does not exist");
                return -1;
        }

        if (parse_config(dotborconf) == -1) {
                return -1;
        }
        return 0;
}

static int parse_config(const char *config_file)
{
        plog(LOG_DEBUG, "parsing config file");

        if (ini_parse(config_file, parse_config_handler, NULL) != 0) {
                plog(LOG_ERROR, "failed parsing config file");
                return -1;
        }

        return 0;
}

static int parse_config_handler(void *UNUSED(user), const char *section,
                                const char *name, const char *value)
{
        int err = 0;
        if (STR_EQUAL(section, "config")) {
                err = section_config_handler(name, value);
        } else if (STR_EQUAL(section, "browsers")) {
                err = section_browsers_handler(name);
        } else {
                plog(LOG_WARN, "unknown config section '%s'", section);
                return 0;
        }

        return (err == -1) ? 0 : 1;
}

// for [config] section of config file
static int section_config_handler(const char *name, const char *value)
{
        if (value == NULL) {
                plog(LOG_ERROR, "key '%s' does not have a value", name);
                return -1;
        }

        for (size_t i = 0; OPTS[i].type != OPT_END; i++) {
                if (!STR_EQUAL(OPTS[i].name, name)) {
                        continue;
                }
                switch (OPTS[i].type) {
                case OPT_BOOL:
                        if (STR_EQUAL(value, "true")) {
                                *(bool *)(OPTS[i].data) = true;
                        } else if (STR_EQUAL(value, "false")) {
                                *(bool *)(OPTS[i].data) = false;
                        } else {
                                plog(LOG_WARN,
                                     "unknown bool value '%s' for '%s',"
                                     " defaulting to false",
                                     value, name);
                                *(bool *)(OPTS[i].data) = false;
                        }
                        break;
                case OPT_INT: {
                        long int num = strtol(value, NULL, 10);

                        *(int *)(OPTS[i].data) = (int)num;
                        break;
                }
                default:
                        continue;
                }
                return 0;
        }
        plog(LOG_WARN, "unknown config option '%s'", name);
        return -1;
}

// for [browsers] section, which only have keys for browser names, no values for each
static int section_browsers_handler(const char *name)
{
        struct Browser *browser = run_browser_sh(name);

        if (browser == NULL) {
                plog(LOG_ERROR, "failed running browser script");
                return -1;
        }
        CONFIG.browsers[CONFIG.browsers_num] = browser;
        (CONFIG.browsers_num)++;
        return 0;
}

// find browser shell script
static struct Browser *run_browser_sh(const char *browsername)
{
        plog(LOG_DEBUG, "running browser shell script for %s", browsername);

        size_t path_suffix_size =
                strlen(browsername) + strlen("scripts/.sh") + 1;
        char path_suffix[path_suffix_size];

        snprintf(path_suffix, path_suffix_size, "scripts/%s.sh", browsername);

        int err = 0;
        size_t found_num = 0;
        // found in order of variadic arguments
        char **found = search_path(&found_num, path_suffix, 3, PATHS.config,
                                   PATHS.share_dir_local, PATHS.share_dir);

        struct Browser *browser = new_browser(browsername, NULL);

        if (browser == NULL) {
                err = -1;
                goto exit;
        }

        if (found == NULL) {
                plog(LOG_ERROR, "failed finding shell script");
                PERROR();
                err = -1;
                goto exit;
        }
        if (found_num == 0) {
                plog(LOG_ERROR, "no shell script found for browser %s",
                     browsername);
                err = -1;
                goto exit;
        }

        plog(LOG_DEBUG, "found %s", found[0]);

        // only use first script found
        if (parse_browser_sh(found[0], browser) == -1) {
                err = -1;
                goto exit;
        }

exit:
        free_str_array(found, found_num);
        free(found);
        if (err == -1) {
                free_browser(browser);
                return NULL;
        }

        return browser;
}

// parse output given by browser shell script
// only initializes procname, dirs and dirs_num members
static int parse_browser_sh(const char *path, struct Browser *browser)
{
        char *command = NULL;

        if (asprintf(&command, "sh %s", path) == -1) {
                return -1;
        }

        FILE *pp = popen(command, "r");

        free(command);
        if (pp == NULL) {
                plog(LOG_ERROR, "failed opening pipe for shell script");
                PERROR();
                return -1;
        }

        if (ini_parse_file(pp, parse_browser_sh_handler, browser) != 0) {
                plog(LOG_ERROR, "failed parsing shell script output");
                pclose(pp);
                return -1;
        }

        pclose(pp);

        // check if procname was given
        if (STR_EQUAL(browser->procname, "")) {
                plog(LOG_ERROR, "browser process name not given");
                return -1;
        }
        // warn if no directories were found
        if (browser->dirs_num == 0) {
                plog(LOG_WARN, "no directories given by shell script");
        }

        return 0;
}

static int parse_browser_sh_handler(void *user, const char *UNUSED(section),
                                    const char *name, const char *value)
{
        if (value == NULL) {
                plog(LOG_ERROR, "key '%s' does not have a value", name);
                return 0;
        }

        struct Browser *browser = user;

        if (STR_EQUAL(name, "procname")) {
                snprintf(browser->procname, PROCNAME_SIZE, "%s", value);
                return 1;
        }
        struct Dir *dir = NULL;

        if (STR_EQUAL(name, "profile")) {
                dir = new_dir(value, DIR_PROFILE, browser);
        } else if (STR_EQUAL(name, "cache")) {
                dir = new_dir(value, DIR_CACHE, browser);
        } else {
                plog(LOG_ERROR, "unknown key '%s'", name);
                return 0;
        }
        if (dir == NULL) {
                plog(LOG_ERROR, "unable to allocate directory structure %s",
                     value);
                return 0;
        }

        add_dir_to_browser(browser, dir);

        return 1;
}

// vim: sw=8 ts=8
